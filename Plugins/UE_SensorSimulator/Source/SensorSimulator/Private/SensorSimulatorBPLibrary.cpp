// Copyright Epic Games, Inc. All Rights Reserved.

#include "SensorSimulatorBPLibrary.h"
#include "SensorSimulator.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TextureRenderTarget2D.h"

#include "Serialization/MemoryWriter.h"

#include <map>

//#include "opencv2/core.hpp"
//#include "opencv2/highgui.hpp"    
//#include "opencv2/imgproc.hpp"
//#include "opencv2/videoio.hpp"

std::map<USceneCaptureComponent2D*, bool> mapCompleted;
bool ShowOnScreenDebugMessages = true;

//ScreenMsg
FORCEINLINE void ScreenMsg(const FString& Msg)
{
	if (!ShowOnScreenDebugMessages) return;
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, *Msg);
}
FORCEINLINE void ScreenMsg(const FString& Msg, const float Value)
{
	if (!ShowOnScreenDebugMessages) return;
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("%s %f"), *Msg, Value));
}
FORCEINLINE void ScreenMsg(const FString& Msg, const FString& Msg2)
{
	if (!ShowOnScreenDebugMessages) return;
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("%s %s"), *Msg, *Msg2));
}

USensorSimulatorBPLibrary::USensorSimulatorBPLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

auto LidarScan = [](const TArray<FLidarPointCloudPoint>& lidarPoints, ULidarPointCloudComponent* lidarPointCloud,
	USceneCaptureComponent2D* sceneCapture,
	FAsyncDelegate Out, FLidarSensorOut& sensorOut,
	const float vFovSDeg, const float vFovEDeg, const int lidarChannels, const float hfovDeg, const int lidarResolution, const float lidarRange)
	{

		FMinimalViewInfo viewInfo;
		sceneCapture->GetCameraView(0, viewInfo);
		UTextureRenderTarget2D* textureRT = sceneCapture->TextureTarget;
		float texWidth = (float)textureRT->SizeX;
		float texHeight = (float)textureRT->SizeY;

		//GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, FString::FromInt(textureRT->SizeX) + ", " + FString::FromInt(textureRT->SizeY));
		//sceneCapture->HiddenActors()

		FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
		UGameplayStatics::GetViewProjectionMatrix(viewInfo, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);

		auto RenderTargetResource = textureRT->GameThread_GetRenderTargetResource();

		TArray<FColor> buffer;
		sensorOut.colorArrayOut.Init(FColor(), texWidth * texHeight);
		if (RenderTargetResource) {
			RenderTargetResource->ReadPixels(buffer);

			sensorOut.colorArrayOut = buffer;
			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, buffer[256 * 100 + 5].ToString());
			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, buffer[256 * 200 + 50].ToString());
			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, buffer[256 * 10 + 200].ToString());
			//cv::Mat wrappedImage(RenderTarget->GetSurfaceHeight(), RenderTarget->GetSurfaceWidth(), CV_8UC4,
			//	buffer.GetData());
			//
			//std::string OutputFile(TCHAR_TO_UTF8(*OutputVideoFile));
			//cv::imwrite(OutputFile, wrappedImage);
		}


		//TObjectPtr<UTextureRenderTarget2D> RTA;
		//RTA.

		UWorld* currentWorld = lidarPointCloud->GetWorld();

		if (currentWorld == nullptr) {
			return;
		}

		sensorOut.lidarPointsOut = lidarPoints; // copy data

		FVector posLidarSensorWS = lidarPointCloud->GetComponentLocation();
		FRotator rotLidarSensor2World = lidarPointCloud->GetComponentRotation();

		const float deltaDeg = hfovDeg / (float)lidarResolution;

		FCollisionObjectQueryParams targetObjTypes;
		targetObjTypes.AddObjectTypesToQuery(ECC_WorldStatic);
		targetObjTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
		targetObjTypes.AddObjectTypesToQuery(ECC_PhysicsBody);
		targetObjTypes.AddObjectTypesToQuery(ECC_Vehicle);
		targetObjTypes.AddObjectTypesToQuery(ECC_Destructible);

		sensorOut.depthArrayOut.Init(-1.f, lidarChannels * lidarResolution);

		const float vRotDelta = fabs(vFovEDeg - vFovSDeg) / std::max((lidarChannels - 1), 1);
		for (int chIdx = 0; chIdx < lidarChannels; chIdx++) {

			float vRotDeg = vFovSDeg + (float)chIdx * vRotDelta;
			FVector dirStart = FVector(1.f, 0, 0).RotateAngleAxis(vRotDeg, FVector(0, -1, 0));
			FVector posChannelStart = posLidarSensorWS;// +FVector(0, 0, 1.f) * channelInterval * (float)chIdx;

			for (int x = 0; x < lidarResolution; x++) {
				float rotDeg = deltaDeg * (float)x - hfovDeg * 0.5f;
				FVector vecRot = dirStart.RotateAngleAxis(rotDeg, FVector(0, 0, 1));
				FVector dirVec = rotLidarSensor2World.RotateVector(vecRot);
				FVector posChannelEnd = posChannelStart + dirVec * lidarRange;

				// LineTrace
				FHitResult outHit;
				if (currentWorld->LineTraceSingleByObjectType(outHit, posChannelStart, posChannelEnd, targetObjTypes)) {
					//FVector3f hitPosition = FVector3f(outHit.Location);
					// we need the local space position rather than world space position
					FVector3f hitPosition = FVector3f(vecRot * outHit.Distance);

					//UMeshComponent* meshComponent = outHit.GetActor()->GetComponentByClass(UMeshComponent::StaticClass());
					//meshComponent->GetMaterial(0);
					//GetMaterialFromInternalFaceIndex()

					//UMeshComponent* meshComponent = (UMeshComponent * )outHit.GetComponent();
					//UMaterialInterface* mat = meshComponent->GetMaterial(0);

					// to do //
					if (RenderTargetResource) {
						FVector3f hitPositionWS = FVector3f(outHit.Location);
						FPlane psPlane = ViewProjectionMatrix.TransformFVector4(FVector4(hitPositionWS, 1.f));
						float NormalizedX = (psPlane.X / (psPlane.W * 2.f)) + 0.5f;
						float NormalizedY = 1.f - (psPlane.Y / (psPlane.W * 2.f)) - 0.5f;
						FVector2D ScreenPos = FVector2D(NormalizedX * texWidth, NormalizedY * texHeight);
						int sx = (int)ScreenPos.X;
						int sy = (int)ScreenPos.Y;
						FColor color(0, 255, 0, 255);
						if (sx >= 0 && sx < texWidth && sy >= 0 && sy < texHeight && psPlane.Z / psPlane.W > 0) {
							color = buffer[sx + sy * texWidth];
						}
						//if (psPlane.Z / psPlane.W > 0) {
						//	color = FColor(255, 0, 0, 255);
						//}
						FLidarPointCloudPoint pcPoint(hitPosition, color, true, 0);
						sensorOut.lidarPointsOut.Add(pcPoint);
					}
					else {
						FLidarPointCloudPoint pcPoint(hitPosition);
						sensorOut.lidarPointsOut.Add(pcPoint);
					}

					sensorOut.depthArrayOut[x + chIdx * lidarResolution] = FVector::Dist(posChannelStart, FVector(hitPosition));
				}
			}
		}
		//GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, FString::FromInt(lidarPointsOut.Num()));
		lidarPointCloud->GetPointCloud()->SetData(sensorOut.lidarPointsOut);

		mapCompleted[sceneCapture] = true;

		AsyncTask(ENamedThreads::GameThread, [Out, &sensorOut]()
			{
				//if (Out != nullptr) {
				// We execute the delegate along with the param
				if (Out.IsBound()) {
					Out.Execute(sensorOut);
				}
				//}

			}
		);
		//
	};

auto LidarScan360 = [](const TArray<FLidarPointCloudPoint>& lidarPoints, ULidarPointCloudComponent* lidarPointCloud,
	USceneCaptureComponent2D* sceneCaptureF, USceneCaptureComponent2D* sceneCaptureL, USceneCaptureComponent2D*
	sceneCaptureB, USceneCaptureComponent2D* sceneCaptureR,
	//USceneCaptureComponent2D* segF,
	//USceneCaptureComponent2D* segR,
	//USceneCaptureComponent2D* segB,
	//USceneCaptureComponent2D* segL,
	FAsyncDelegate360 Out, FLidarSensorOut360& sensorOut360,
	const float vFovSDeg, const float vFovEDeg, const int lidarChannels, const float hfovDeg, const int lidarResolution, const float lidarRange)
	{

		FMinimalViewInfo viewInfoF;
		FMinimalViewInfo viewInfoL;
		FMinimalViewInfo viewInfoB;
		FMinimalViewInfo viewInfoR;

		sceneCaptureF->GetCameraView(0, viewInfoF);
		sceneCaptureL->GetCameraView(0, viewInfoL);
		sceneCaptureB->GetCameraView(0, viewInfoB);
		sceneCaptureR->GetCameraView(0, viewInfoR);

		UTextureRenderTarget2D* textureRTF = sceneCaptureF->TextureTarget;
		UTextureRenderTarget2D* textureRTL = sceneCaptureL->TextureTarget;
		UTextureRenderTarget2D* textureRTB = sceneCaptureB->TextureTarget;
		UTextureRenderTarget2D* textureRTR = sceneCaptureR->TextureTarget;

		float texWidth = (float)textureRTF->SizeX;
		float texHeight = (float)textureRTF->SizeY;

		FMatrix ViewMatrixF, ProjectionMatrixF, ViewProjectionMatrixF;
		FMatrix ViewMatrixL, ProjectionMatrixL, ViewProjectionMatrixL;
		FMatrix ViewMatrixB, ProjectionMatrixB, ViewProjectionMatrixB;
		FMatrix ViewMatrixR, ProjectionMatrixR, ViewProjectionMatrixR;

		UGameplayStatics::GetViewProjectionMatrix(viewInfoF, ViewMatrixF, ProjectionMatrixF, ViewProjectionMatrixF);
		UGameplayStatics::GetViewProjectionMatrix(viewInfoL, ViewMatrixL, ProjectionMatrixL, ViewProjectionMatrixL);
		UGameplayStatics::GetViewProjectionMatrix(viewInfoB, ViewMatrixB, ProjectionMatrixB, ViewProjectionMatrixB);
		UGameplayStatics::GetViewProjectionMatrix(viewInfoR, ViewMatrixR, ProjectionMatrixR, ViewProjectionMatrixR);

		auto RenderTargetResourceF = textureRTF->GameThread_GetRenderTargetResource();
		auto RenderTargetResourceL = textureRTL->GameThread_GetRenderTargetResource();
		auto RenderTargetResourceB = textureRTB->GameThread_GetRenderTargetResource();
		auto RenderTargetResourceR = textureRTR->GameThread_GetRenderTargetResource();

		TArray<FColor> bufferF, bufferL, bufferB, bufferR;
		if (sensorOut360.colorArrayOutF.Num() == 0) {
			sensorOut360.colorArrayOutF.Init(FColor(), texWidth * texHeight);
			sensorOut360.colorArrayOutL.Init(FColor(), texWidth * texHeight);
			sensorOut360.colorArrayOutB.Init(FColor(), texWidth * texHeight);
			sensorOut360.colorArrayOutR.Init(FColor(), texWidth * texHeight);
			//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString::Printf(TEXT("created")));
		}


		if (RenderTargetResourceF) {
			RenderTargetResourceF->ReadPixels(bufferF);
			sensorOut360.colorArrayOutF = bufferF;
		}
		if (RenderTargetResourceL) {
			RenderTargetResourceL->ReadPixels(bufferL);
			sensorOut360.colorArrayOutL = bufferL;
		}
		if (RenderTargetResourceB) {
			RenderTargetResourceB->ReadPixels(bufferB);
			sensorOut360.colorArrayOutB = bufferB;
		}
		if (RenderTargetResourceR) {
			RenderTargetResourceR->ReadPixels(bufferR);
			sensorOut360.colorArrayOutR = bufferR;
		}

		UWorld* currentWorld = lidarPointCloud->GetWorld();

		if (currentWorld == nullptr) {
			return;
		}

		sensorOut360.lidarPointsOut360 = lidarPoints; // copy data

		FVector posLidarSensorWS = lidarPointCloud->GetComponentLocation();
		FRotator rotLidarSensor2World = lidarPointCloud->GetComponentRotation();

		const float deltaDeg = hfovDeg / (float)lidarResolution;

		FCollisionObjectQueryParams targetObjTypes;
		targetObjTypes.AddObjectTypesToQuery(ECC_WorldStatic);
		targetObjTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
		targetObjTypes.AddObjectTypesToQuery(ECC_PhysicsBody);
		targetObjTypes.AddObjectTypesToQuery(ECC_Vehicle);
		targetObjTypes.AddObjectTypesToQuery(ECC_Destructible);

		if (sensorOut360.depthArrayOut360.Num() == 0) {
			sensorOut360.depthArrayOut360.Init(-1.f, lidarChannels * lidarResolution);
			//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Green, FString::Printf(TEXT("created")));
		}

		const float vRotDelta = fabs(vFovEDeg - vFovSDeg) / std::max((lidarChannels - 1), 1);
		for (int chIdx = 0; chIdx < lidarChannels; chIdx++) {

			float vRotDeg = vFovSDeg + (float)chIdx * vRotDelta;
			FVector dirStart = FVector(1.f, 0, 0).RotateAngleAxis(vRotDeg, FVector(0, -1, 0));
			FVector posChannelStart = posLidarSensorWS;// +FVector(0, 0, 1.f) * channelInterval * (float)chIdx;

			for (int x = 0; x < lidarResolution; x++) {
				float rotDeg = deltaDeg * (float)x - hfovDeg * 0.5f;
				FVector vecRot = dirStart.RotateAngleAxis(rotDeg, FVector(0, 0, 1));
				FVector dirVec = rotLidarSensor2World.RotateVector(vecRot);
				FVector posChannelEnd = posChannelStart + dirVec * lidarRange;

				// LineTrace
				FHitResult outHit;
				if (currentWorld->LineTraceSingleByObjectType(outHit, posChannelStart, posChannelEnd, targetObjTypes)) {
					FVector3f hitPosition = FVector3f(vecRot * outHit.Distance);
					FColor color(255, 255, 0, 255);

					//if (RenderTargetResourceF) {
					//	FVector3f hitPositionWS = FVector3f(outHit.Location);
					//	FPlane psPlane = ViewProjectionMatrixF.TransformFVector4(FVector4(hitPositionWS, 1.f));
					//	float NormalizedX = (psPlane.X / (psPlane.W * 2.f)) + 0.5f;
					//	float NormalizedY = 1.f - (psPlane.Y / (psPlane.W * 2.f)) - 0.5f;
					//	FVector2D ScreenPos = FVector2D(NormalizedX * texWidth, NormalizedY * texHeight);
					//	int sx = (int)ScreenPos.X;
					//	int sy = (int)ScreenPos.Y;
					//	if (sx >= 0 && sx < texWidth && sy >= 0 && sy < texHeight && psPlane.Z / psPlane.W > 0) {
					//		//color = bufferF[sx + sy * texWidth];
					//		FLidarPointCloudPoint pcPoint(hitPosition, color, true, 0);
					//		sensorOut360.lidarPointsOut360.Add(pcPoint);
					//	}
					//}
					//if (RenderTargetResourceL) {
					//	FVector3f hitPositionWS = FVector3f(outHit.Location);
					//	FPlane psPlane = ViewProjectionMatrixL.TransformFVector4(FVector4(hitPositionWS, 1.f));
					//	float NormalizedX = (psPlane.X / (psPlane.W * 2.f)) + 0.5f;
					//	float NormalizedY = 1.f - (psPlane.Y / (psPlane.W * 2.f)) - 0.5f;
					//	FVector2D ScreenPos = FVector2D(NormalizedX * texWidth, NormalizedY * texHeight);
					//	int sx = (int)ScreenPos.X;
					//	int sy = (int)ScreenPos.Y;
					//	if (sx >= 0 && sx < texWidth && sy >= 0 && sy < texHeight && psPlane.Z / psPlane.W > 0) {
					//		//color = bufferL[sx + sy * texWidth];
					//		FLidarPointCloudPoint pcPoint(hitPosition, color, true, 0);
					//		sensorOut360.lidarPointsOut360.Add(pcPoint);
					//	}
					//}
					//if (RenderTargetResourceB) {
					//	FVector3f hitPositionWS = FVector3f(outHit.Location);
					//	FPlane psPlane = ViewProjectionMatrixB.TransformFVector4(FVector4(hitPositionWS, 1.f));
					//	float NormalizedX = (psPlane.X / (psPlane.W * 2.f)) + 0.5f;
					//	float NormalizedY = 1.f - (psPlane.Y / (psPlane.W * 2.f)) - 0.5f;
					//	FVector2D ScreenPos = FVector2D(NormalizedX * texWidth, NormalizedY * texHeight);
					//	int sx = (int)ScreenPos.X;
					//	int sy = (int)ScreenPos.Y;
					//	if (sx >= 0 && sx < texWidth && sy >= 0 && sy < texHeight && psPlane.Z / psPlane.W > 0) {
					//		//color = bufferB[sx + sy * texWidth];
					//		FLidarPointCloudPoint pcPoint(hitPosition, color, true, 0);
					//		sensorOut360.lidarPointsOut360.Add(pcPoint);
					//	}
					//}
					//if (RenderTargetResourceR) {
					//	FVector3f hitPositionWS = FVector3f(outHit.Location);
					//	FPlane psPlane = ViewProjectionMatrixR.TransformFVector4(FVector4(hitPositionWS, 1.f));
					//	float NormalizedX = (psPlane.X / (psPlane.W * 2.f)) + 0.5f;
					//	float NormalizedY = 1.f - (psPlane.Y / (psPlane.W * 2.f)) - 0.5f;
					//	FVector2D ScreenPos = FVector2D(NormalizedX * texWidth, NormalizedY * texHeight);
					//	int sx = (int)ScreenPos.X;
					//	int sy = (int)ScreenPos.Y;
					//	if (sx >= 0 && sx < texWidth && sy >= 0 && sy < texHeight && psPlane.Z / psPlane.W > 0) {
					//		//color = bufferR[sx + sy * texWidth];
					//		FLidarPointCloudPoint pcPoint(hitPosition, color, true, 0);
					//		sensorOut360.lidarPointsOut360.Add(pcPoint);
					//	}
					//}
					//else {
					//	FLidarPointCloudPoint pcPoint(hitPosition);
					//	sensorOut360.lidarPointsOut360.Add(pcPoint);
					//}
					FLidarPointCloudPoint pcPoint(hitPosition, color, true, 0);
					sensorOut360.lidarPointsOut360.Add(pcPoint);

					//sensorOut360.depthArrayOut360[x + chIdx * lidarResolution] = FVector::Dist(posChannelStart, FVector(hitPosition));
					sensorOut360.depthArrayOut360[x + chIdx * lidarResolution] = outHit.Distance;
				}
			}
		}
		lidarPointCloud->GetPointCloud()->SetData(sensorOut360.lidarPointsOut360);

		mapCompleted[sceneCaptureF] = true;

		AsyncTask(ENamedThreads::GameThread, [Out, &sensorOut360]()
			{
				if (Out.IsBound()) {
					Out.Execute(sensorOut360);
				}
			}
		);
	};

void USensorSimulatorBPLibrary::LidarSensorAsyncScan360(
	const TArray<FLidarPointCloudPoint>& lidarPoints, ULidarPointCloudComponent* lidarPointCloud,
	USceneCaptureComponent2D* sceneCaptureF, USceneCaptureComponent2D* sceneCaptureL,
	USceneCaptureComponent2D* sceneCaptureB, USceneCaptureComponent2D* sceneCaptureR,
	FAsyncDelegate360 Out, FLidarSensorOut360& sensorOut, const bool asyncScan,
	const float vFovSDeg, const float vFovEDeg, const int lidarChannels, const float hfovDeg,
	const int lidarResolution, const float lidarRange)
{
	if (mapCompleted.find(sceneCaptureF) == mapCompleted.end())
		mapCompleted[sceneCaptureF] = true;
	if (!mapCompleted[sceneCaptureF]) {
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Wait..."));
		return;
	}
	mapCompleted[sceneCaptureF] = false;

	if (asyncScan) {
		// Schedule a thread
		// Pass in our parameters to the lambda expression
		// note that those parameters from the main thread can be released out during this async process
		// pointer parameters are safe because they are conservative
		AsyncTask(ENamedThreads::GameThread, [&lidarPoints, lidarPointCloud, sceneCaptureF, sceneCaptureL, sceneCaptureB, sceneCaptureR,
			Out, &sensorOut, asyncScan, vFovSDeg, vFovEDeg, lidarChannels, hfovDeg, lidarResolution, lidarRange]() { // AnyHiPriThreadNormalTask
				LidarScan360(lidarPoints, lidarPointCloud, sceneCaptureF, sceneCaptureL, sceneCaptureB, sceneCaptureR,
				//segF, segR, segB, segL
					Out, sensorOut,
					vFovSDeg, vFovEDeg, lidarChannels, hfovDeg, lidarResolution, lidarRange);
			});
	}
	else {
		LidarScan360(lidarPoints, lidarPointCloud, sceneCaptureF, sceneCaptureL, sceneCaptureB, sceneCaptureR,
			//segF, segR, segB, segL,
			Out, sensorOut,
			vFovSDeg, vFovEDeg, lidarChannels, hfovDeg, lidarResolution, lidarRange);
	}
}

void USensorSimulatorBPLibrary::LidarSensorAsyncScan4(
	const TArray<FLidarPointCloudPoint>& lidarPoints, ULidarPointCloudComponent* lidarPointCloud, USceneCaptureComponent2D* sceneCapture,
	FAsyncDelegate Out, FLidarSensorOut& sensorOut, const bool asyncScan,
	const float vFovSDeg, const float vFovEDeg, const int lidarChannels, const float hfovDeg, const int lidarResolution, const float lidarRange
)
{
	if (mapCompleted.find(sceneCapture) == mapCompleted.end())
		mapCompleted[sceneCapture] = true;
	if (!mapCompleted[sceneCapture]) {
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Wait..."));
		return;
	}
	mapCompleted[sceneCapture] = false;

	if (asyncScan) {
		// Schedule a thread
		// Pass in our parameters to the lambda expression
		// note that those parameters from the main thread can be released out during this async process
		// pointer parameters are safe because they are conservative
		AsyncTask(ENamedThreads::GameThread, [&lidarPoints, lidarPointCloud, sceneCapture, Out, &sensorOut, asyncScan, vFovSDeg, vFovEDeg, lidarChannels, hfovDeg, lidarResolution, lidarRange]() { // AnyHiPriThreadNormalTask
			LidarScan(lidarPoints, lidarPointCloud, sceneCapture, Out, sensorOut,
			vFovSDeg, vFovEDeg, lidarChannels, hfovDeg, lidarResolution, lidarRange);
			});
	}
	else {
		LidarScan(lidarPoints, lidarPointCloud, sceneCapture, Out, sensorOut,
			vFovSDeg, vFovEDeg, lidarChannels, hfovDeg, lidarResolution, lidarRange);
	}
}



void USensorSimulatorBPLibrary::ReadSemantic(USceneCaptureComponent2D* sceneCapture, TArray<FColor>& semanticArrayOut) {
	UTextureRenderTarget2D* textureRT = sceneCapture->TextureTarget;
	float texWidth = (float)textureRT->SizeX;
	float texHeight = (float)textureRT->SizeY;

	auto RenderTargetResource = textureRT->GameThread_GetRenderTargetResource();

	TArray<FColor> buffer;
	semanticArrayOut.Init(FColor(), texWidth * texHeight);
	if (RenderTargetResource) {
		RenderTargetResource->ReadPixels(buffer);
		semanticArrayOut = buffer;
	}
}

void USensorSimulatorBPLibrary::SensorOutToBytes(const TArray<FLidarSensorOut>& lidarSensorOuts,
	TArray<FBytes>& bytePackets, FString& bytesInfo, int& bytesPoints, int& bytesColorMap,
	int& bytesDepthMap, const int packetBytes)
{
	// compute array size
	bytesPoints = 0;
	bytesColorMap = 0;
	bytesDepthMap = 0;
	int index = 0;
	for (const FLidarSensorOut& sensorOut : lidarSensorOuts) {
		if (sensorOut.lidarPointsOut.Num() > 0) {
			bytesPoints += sensorOut.lidarPointsOut.Num() * 4 * 4 + 4;
			bytesInfo += FString("Point") + FString::FromInt(index) + FString(":") + FString::FromInt(sensorOut.lidarPointsOut.Num()) + FString("//");
		}
		if (sensorOut.depthArrayOut.Num() > 0) {
			bytesDepthMap += sensorOut.depthArrayOut.Num() * 4;
			bytesInfo += FString("Depth") + FString::FromInt(index) + FString(":") + FString::FromInt(sensorOut.depthArrayOut.Num()) + FString("//");
		}
		if (sensorOut.colorArrayOut.Num() > 0) {
			bytesColorMap += sensorOut.colorArrayOut.Num() * 4;
			bytesInfo += FString("Color") + FString::FromInt(index) + FString(":") + FString::FromInt(sensorOut.colorArrayOut.Num()) + FString("//");
		}
		index++;
	}
	uint32 bytesCount = bytesPoints + bytesColorMap + bytesDepthMap;
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString::FromInt(bytesCount));
	TArray<uint8> bytes;
	bytes.Init(0, bytesCount);
	uint32 offsetPoints = 0, offsetColorMap = 0, offsetDepthMap = 0;
	for (const FLidarSensorOut& sensorOut : lidarSensorOuts) {
		if (sensorOut.lidarPointsOut.Num() > 0) {
			*(int*)&bytes[offsetPoints] = sensorOut.lidarPointsOut.Num();
			offsetPoints += 4;
			for (const FLidarPointCloudPoint& pp : sensorOut.lidarPointsOut) {
				memcpy(&bytes[offsetPoints], &(pp.Location), 4 * 3);
				offsetPoints += 12;
				memcpy(&bytes[offsetPoints], &(pp.Color.DWColor()), 4);
				offsetPoints += 4;
			}
		}
		if (sensorOut.depthArrayOut.Num() > 0) {
			memcpy(&bytes[offsetDepthMap + bytesPoints], &sensorOut.depthArrayOut[0], sensorOut.depthArrayOut.Num() * 4);
			offsetDepthMap += sensorOut.depthArrayOut.Num() * 4;
		}
		if (sensorOut.colorArrayOut.Num() > 0) {
			memcpy(&bytes[offsetColorMap + bytesPoints + bytesDepthMap], &sensorOut.colorArrayOut[0], sensorOut.colorArrayOut.Num() * 4);
			offsetColorMap += sensorOut.colorArrayOut.Num() * 4;
		}
	}
	//TArray<TArray<uint8>> bytePackets;
	int numPackets = (int)ceil((float)bytesCount / (float)packetBytes);
	bytePackets.Init(FBytes(), numPackets);
	int remainingBytes = bytesCount;
	int offset = 0;
	int packetIndex = 0;
	for (FBytes& packet : bytePackets) {
		int packetSize = std::min(packetBytes, remainingBytes);
		packet.ArrayOut.Init(0, packetSize + 4);
		*(int*)&packet.ArrayOut[0] = packetIndex++;
		memcpy(&packet.ArrayOut[4], &bytes[offset], packetSize);

		remainingBytes -= packetSize;
		offset += packetSize;
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::FromInt(packetSize));
	}
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Blue, FString::FromInt(packetBytes));
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, FString::FromInt(remainingBytes));
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Green, FString::FromInt(offset));
}

void USensorSimulatorBPLibrary::SensorOutToBytes360(const FLidarSensorOut360& lidarSensorOuts,
	//TArray<FBytes>& bytePackets,
	FTArrayBytes& bytePackets,
	FString& bytesInfo, int& bytesPoints, int& bytesColorMap,
	int& bytesDepthMap, const int packetBytes)
{

	// compute array size
	//bytesPoints = 0;
	bytesColorMap = 0;
	bytesDepthMap = 0;

	int index = 0;

	//if (lidarSensorOuts.lidarPointsOut360.Num() > 0) {
	//	bytesPoints += lidarSensorOuts.lidarPointsOut360.Num() * 4 * 4 + 4;
	//	bytesInfo += FString("Point") + FString::FromInt(index) + FString(":") + FString::FromInt(lidarSensorOuts.lidarPointsOut360.Num()) + FString("//");
	//}
	if (lidarSensorOuts.depthArrayOut360.Num() > 0) {
		bytesDepthMap += lidarSensorOuts.depthArrayOut360.Num() * 4;
		bytesInfo += FString("Depth") + FString::FromInt(index) + FString(":") + FString::FromInt(lidarSensorOuts.depthArrayOut360.Num()) + FString("//");
	}
	if (lidarSensorOuts.colorArrayOutF.Num() > 0) {
		bytesColorMap += lidarSensorOuts.colorArrayOutF.Num() * 4;
		bytesInfo += FString("ColorF") + FString::FromInt(index) + FString(":") + FString::FromInt(lidarSensorOuts.colorArrayOutF.Num()) + FString("//");
	}
	if (lidarSensorOuts.colorArrayOutR.Num() > 0) {
		bytesColorMap += lidarSensorOuts.colorArrayOutR.Num() * 4;
		bytesInfo += FString("ColorR") + FString::FromInt(index) + FString(":") + FString::FromInt(lidarSensorOuts.colorArrayOutR.Num()) + FString("//");
	}
	if (lidarSensorOuts.colorArrayOutB.Num() > 0) {
		bytesColorMap += lidarSensorOuts.colorArrayOutB.Num() * 4;
		bytesInfo += FString("ColorB") + FString::FromInt(index) + FString(":") + FString::FromInt(lidarSensorOuts.colorArrayOutB.Num()) + FString("//");
	}
	if (lidarSensorOuts.colorArrayOutL.Num() > 0) {
		bytesColorMap += lidarSensorOuts.colorArrayOutL.Num() * 4;
		bytesInfo += FString("ColorL") + FString::FromInt(index) + FString(":") + FString::FromInt(lidarSensorOuts.colorArrayOutL.Num()) + FString("//");
	}

	//uint32 bytesCount = bytesPoints + bytesColorMap * 4 + bytesDepthMap;
	//uint32 bytesCount = bytesColorMap * 4 + bytesDepthMap;
	uint32 bytesCount = bytesColorMap + bytesDepthMap;

	TArray<uint8> bytes;
	bytes.Init(0, bytesCount);
	//uint32 offsetPoints = 0, offsetColorMap = 0, offsetDepthMap = 0;
	uint32 offsetColorMap = 0, offsetDepthMap = 0;

	if (lidarSensorOuts.depthArrayOut360.Num() > 0) {
		memcpy(&bytes[offsetDepthMap], &lidarSensorOuts.depthArrayOut360[0], lidarSensorOuts.depthArrayOut360.Num() * 4);
		offsetDepthMap += lidarSensorOuts.depthArrayOut360.Num() * 4;
		//offsetColorMap += offsetDepthMap; //my
	}
	if (lidarSensorOuts.colorArrayOutF.Num() > 0) {
		memcpy(&bytes[offsetColorMap + bytesDepthMap], &lidarSensorOuts.colorArrayOutF[0], lidarSensorOuts.colorArrayOutF.Num() * 4);
		offsetColorMap += lidarSensorOuts.colorArrayOutF.Num() * 4;
	}
	if (lidarSensorOuts.colorArrayOutR.Num() > 0) {
		memcpy(&bytes[offsetColorMap + bytesDepthMap], &lidarSensorOuts.colorArrayOutR[0], lidarSensorOuts.colorArrayOutR.Num() * 4);
		offsetColorMap += lidarSensorOuts.colorArrayOutR.Num() * 4;
	}
	if (lidarSensorOuts.colorArrayOutB.Num() > 0) {
		memcpy(&bytes[offsetColorMap + bytesDepthMap], &lidarSensorOuts.colorArrayOutB[0], lidarSensorOuts.colorArrayOutB.Num() * 4);
		offsetColorMap += lidarSensorOuts.colorArrayOutB.Num() * 4;
	}
	if (lidarSensorOuts.colorArrayOutL.Num() > 0) {
		memcpy(&bytes[offsetColorMap + bytesDepthMap], &lidarSensorOuts.colorArrayOutL[0], lidarSensorOuts.colorArrayOutL.Num() * 4);
		offsetColorMap += lidarSensorOuts.colorArrayOutL.Num() * 4;
	}

	//off set 더해주면서 이동

	//TArray<TArray<uint8>> bytePackets;
	int numPackets = (int)ceil((float)bytesCount / (float)packetBytes);

	//bytePackets.ArrayOut.Reset();
	if (bytePackets.FArrayOut.Num() == 0) {
		bytePackets.FArrayOut.Init(FBytes(), numPackets);
		//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Blue, FString::Printf(TEXT("created")));
	}

	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Blue, FString::Printf(TEXT("created")));
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Blue, FString::Printf(TEXT("passed")));


	int remainingBytes = bytesCount;
	int offset = 0;
	int packetIndex = 0;

	int testcount = 0;
	for (FBytes& packet : bytePackets.FArrayOut) {
		int packetSize = std::min(packetBytes, remainingBytes);
		//packet.ArrayOut.Init(0, packetSize + 8);
		if (packet.ArrayOut.Num() == 0) {
			packet.ArrayOut.Init(0, packetSize + 4);
			//testcount++;
		}
		/*	else {
				testcount = 0;
			}*/
			//*(int*)&packet.ArrayOut[0] = GFrameNumber;
		*(int*)&packet.ArrayOut[0] = packetIndex++;
		//*(int*)&packet.ArrayOut[4] = packetIndex++;
		memcpy(&packet.ArrayOut[4], &bytes[offset], packetSize);

		remainingBytes -= packetSize;
		offset += packetSize;
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::FromInt(packetSize));
	}
	//UE_LOG(LogTemp, Log, TEXT("%d"), static_cast<int32>(frameIndex));
	//UE_LOG(LogTemp, Log, TEXT("hihi"));
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Blue, FString::FromInt(testcount));
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, FString::FromInt(remainingBytes));
	//GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Green, FString::FromInt(offset));
}



void USensorSimulatorBPLibrary::IntToBytes(const int fromInt, TArray <uint8>& bytes)
{
	bytes.Init(0, 4);
	*(int*)&bytes[0] = fromInt;
}

void USensorSimulatorBPLibrary::GetViewProTectionMatrix(const TArray<USceneCaptureComponent2D*> sceneCapture,
	TArray<FMatrix>& viewprojectionMatrix, TArray<FMatrix>& projectionMatrix, TArray<FMatrix>& viewMatrix)
{
	for (int i = 0; i < sceneCapture.Num(); i++) {
		USceneCaptureComponent2D* sceneCapture2 = sceneCapture[i];
		FMinimalViewInfo viewInfo;
		sceneCapture2->GetCameraView(0, viewInfo);
		UTextureRenderTarget2D* textureRT = sceneCapture2->TextureTarget;
		float texWidth = (float)textureRT->SizeX;
		float texHeight = (float)textureRT->SizeY;
		UGameplayStatics::GetViewProjectionMatrix(viewInfo, viewMatrix[i], projectionMatrix[i], viewprojectionMatrix[i]);
	}
}

void USensorSimulatorBPLibrary::PosAndRotToBytes(const FVector position, const FRotator rotation, TArray<uint8>& bytePackets)
{
	bytePackets.Init(0, sizeof(FVector) + sizeof(FRotator));

	uint8* DataPtr = bytePackets.GetData();
	FMemory::Memcpy(DataPtr, &position, sizeof(FVector));
	DataPtr += sizeof(FVector);
	FMemory::Memcpy(DataPtr, &rotation, sizeof(FRotator));
	//FMemory::Memcpy(DataPtr, reinterpret_cast<const uint8*>(&position), sizeof(FVector));
	//FMemory::Memcpy(DataPtr, reinterpret_cast<const uint8*>(&rotation), sizeof(FRotator));
}

void USensorSimulatorBPLibrary::CamInfoToBytes(const TArray<FMatrix>& camMat, const int fov, const float aspectRatio, TArray<uint8>& bytePackets)
{
	for (const FMatrix& Matrix : camMat)
	{
		// Convert FMatrix to bytes
		const double* MatrixPtr = Matrix.M[0];
		for (int32 i = 0; i < 16; ++i)
		{
			bytePackets.Add(reinterpret_cast<const uint8*>(MatrixPtr)[i]);
		}
	}
	bytePackets.Append(reinterpret_cast<const uint8*>(&aspectRatio), sizeof(float));
	bytePackets.Append(reinterpret_cast<const uint8*>(&fov), sizeof(int));
}

//for test PosAndRotToBytes
void USensorSimulatorBPLibrary::ConvertBytesToVectorAndRotator(const TArray<uint8>& InBytes, FVector& OutVector, FRotator& OutRotator)
{
	check(InBytes.Num() == sizeof(FVector) + sizeof(FRotator));

	uint8* DataPtr = const_cast<uint8*>(InBytes.GetData());
	FMemory::Memcpy(&OutVector, DataPtr, sizeof(FVector));
	DataPtr += sizeof(FVector);
	FMemory::Memcpy(&OutRotator, DataPtr, sizeof(FRotator));
}
