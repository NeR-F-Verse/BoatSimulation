# UnrealEngine5 기반 선박운항 시뮬레이터를 통한 2.5D 서라운드뷰 생성

## 선박 운항 시뮬레이션

### A. 선박 모델

- 10종 이상의 다양한 선박 모델 사용

### B. 환경 조명 효과

- 시간별(낮/새벽/저녁/밤) 및 날씨별(태풍/흐림/맑음/안개) 사실적 조명효과 적용

### C. 물리 시뮬레이션

- 운항 관련 물리 시뮬레이션(파고 높이, 부력, 충돌) 적용

### D. 렌더링 및 UI

- World (Cinematic) 렌더링 및 사용자 설정 UI 지원

## 센서 시뮬레이션

### A. RGB 센서

1. 카메라별 (렌더링된) RGB 영상 출력
2. Intrinsic Parameters 입력 설정
3. Realism Filter 적용 (AI 모델) (Postprocessing)
4. 센서 적용 범위 가시화 (View Frustum)

### B. Lidar 센서

1. 해상도 및 채널 수에 따른 Ray-hit Point Cloud 출력
2. 획득한 Point Cloud 별도 가시화
3. 센서 적용 범위 가시화 (Sensor Rays)

### C. GPS

1. 개별 센서의 월드에서의 위치 정보 출력 (정밀도 제한 처리)

## 가상 센서 캘리브레이션

### A. 캘리브레이션 모듈

- RGB 영상의 Spatial (다중 카메라) & Temporal (선박 운항에 따른 카메라 이동) Feature Mapping을 통한 카메라 Parameters 추정

### B. 수평 보정

- 센서 캡쳐 범위 수평 보정

## RGBD 영상 생성

### A. 저해상도 Depth Map 생성

- Lidar 센서 기준 저해상도 Depth Map 생성 - Lidar Sensor Point Cloud를 고해상도 RGB 이미지에 매핑

### B. 고해상도 Depth Map 생성

- RGB to RGBD 추정 기술 적용(AI 모델)
