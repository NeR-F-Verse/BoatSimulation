// Fill out your copyright notice in the Description page of Project Settings.


#include "MyBPtest.h"
#include <windows.h>
#include "opencv2/opencv.hpp"
#include <iostream>
#include <vector>


void UMyBPtest::GetDepthMap(int channel, int channel_distance, int raydistance, int resolution, TArray<float> depth)
{
	int arraysize = (resolution) * (channel + 1);
	float normalize_depth = raydistance / 255;
	
	UE_LOG(LogTemp, Log, TEXT("cpp function Called, arraysize = %d"),arraysize);
	//std::cout << "OpenCV version : " << cv::getVersionString << std::endl;
	for (int i = 0; i < arraysize; i++) {
		depth[i] = 1 - depth[i] / raydistance; //0~1사이
		UE_LOG(LogTemp, Log, TEXT("result = %f"), depth[i]);
	}
	cv::Mat greyImg = cv::Mat(channel + 1, resolution, CV_32F, depth.GetData());
	cv::Mat greyImg2;
	cv::flip(greyImg, greyImg2, 0);	
	cv::imshow("img", greyImg2);
	cv::waitKey();
	UE_LOG(LogTemp, Log, TEXT("finish"));
}
void UMyBPtest::GetPointCloud(int ViewWidth, int ViewHight, int FarPlane, int NearPlane, int imgNum, int channel, int resolution, TArray<FVector> points, TArray<float> depth)
{
	 
	//UE_LOG(LogTemp, Log, TEXT("aaaa = %f"), channel);
	// 주어진 정보로 viewport transform 구현
	::Sleep(3000);
	cv::Mat img3 = cv::imread("C:\\Users\\drago\\university\\lab\\UE_pointcloud\\0.hdr", cv::IMREAD_ANYDEPTH | cv::IMREAD_COLOR);

	cv::Mat depthMat = cv::Mat(channel + 1, resolution, CV_32F, depth.GetData());
	cv::flip(depthMat, depthMat, 0); //2차원으로 만들 필요 없을 듯 

	float* depth2 = depth.GetData();

	if (img3.empty())
		std::cout << "failed to open img" << std::endl;
	else {
		/*cv::Mat ldr;
		cv::Ptr<cv::TonemapReinhard> tonemap = cv::createTonemapReinhard(2.2f);
		tonemap->process(img, ldr);
		ldr.convertTo(ldr, CV_8UC3, 255);*/
		/*
		cv::putText(img3,
			"test",
			cv::Point(10, img3.rows / 2), //점 위치 + 10
			cv::FONT_HERSHEY_DUPLEX,
			0.1, //font size
			CV_RGB(118, 185, 0), //font color
			0.1);
		cv::circle(img3, cv::Point(), 3, cv::Scalar::all(0), -1);
		*/

		for (int i = 0; points.Num(); i++) {
			std::cout << points[i][0] << std::endl;
			cv::putText(img3,
				std::to_string(depth[i]),
				cv::Point(10 + points[i][0], points[i][1]), //점 위치 + 10
				cv::FONT_HERSHEY_DUPLEX,
				0.1, //font size 
				CV_RGB(118, 185, 0), //font color
				0.1);
			cv::circle(img3, cv::Point(points[i][0], points[i][1]), 1, cv::Scalar::all(0), -1);
		}


		cv::Mat ldr;
		cv::Ptr<cv::TonemapReinhard> tonemap = cv::createTonemapReinhard(2.2f);
		tonemap->process(img3, ldr);
		//ldr.convertTo(ldr, CV_8UC3, 255);
		cv::imshow("img", ldr);
		cv::waitKey();
	};
}