// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#pragma warning( disable : 4668 )

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyBPtest.generated.h"

/**
 * 
 */
UCLASS()
class BOATSIM_API UMyBPtest : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
		UFUNCTION(BlueprintCallable)
		//static void EasyPlus(int channel, int channel_distance, int reslution, float depth[]);
		static void GetDepthMap(int channel, int channel_distance, int raydistance, int resolution, TArray<float> depth);
		UFUNCTION(BlueprintCallable)
		static void GetPointCloud(int ViewWidth, int ViewHight, int FarPlane, int NearPlane, int imgNum, int channel, int resolution, TArray<FVector> points, TArray<float> depth);
};
