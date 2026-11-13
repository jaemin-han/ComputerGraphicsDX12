# 레이 씬 텍스처

2026-11-13에 준비했다. 텍스처 3개 합계는 14,449,165바이트
(14.45 MB / 13.78 MiB)다. 모든 파일은 CC0 원본에서 변환했다.
Part 2 강의 자산은 사용하지 않았다.

## 파일과 사용 위치

| 파일 | 슬롯 / 용도 | 해상도 | 픽셀 형식 | 크기 |
| --- | --- | --- | --- | ---: |
| [kloofendal_specular.dds](textures/kloofendal_specular.dds) | t2 / 하늘, 선명한 반사·굴절 환경 | 512 × 512 × 6면 | R16G16B16A16_FLOAT | 12,583,060 B |
| [kloofendal_diffuse.dds](textures/kloofendal_diffuse.dds) | t1 / 확산 환경 조명 | 32 × 32 × 6면 | R16G16B16A16_FLOAT | 49,300 B |
| [cobblestone_albedo.png](textures/cobblestone_albedo.png) | t3 / 반복 바닥 albedo | 1024 × 1024 | PNG RGBA, 채널당 8비트 | 1,816,805 B |

두 DDS는 압축하지 않은 선형 HDR RGB와 alpha 1을 저장한다.
밉 레벨은 각각 1개(mip 0)다. t2는 roughness에 따른 specular prefilter가
아니라 원본 환경의 해상도만 낮춘 맵이다.

## 출처와 변환

### kloofendal_specular.dds

- 출처: Poly Haven, [Kloofendal 48d Partly Cloudy](https://polyhaven.com/a/kloofendal_48d_partly_cloudy).
- 작가: Greg Zaal.
- 라이선스: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
  [Poly Haven 라이선스 안내](https://polyhaven.com/license)는 상업 이용과
  자산을 포함한 재배포를 허용한다. 공개 포트폴리오 저장소에 포함할 수 있다.
- 원본 파일명: `kloofendal_48d_partly_cloudy_4k.hdr`.
- [원본 다운로드](https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_4k.hdr).
- 원본: 4096 × 2048 Radiance RGBE, equirectangular, 헤더 방향 `-Y +X`.
- 원본 MD5: `afb5e5f0c035d24567e8d778dd35c752`.
  Poly Haven 파일 API의 값과 다운로드 파일이 일치했다.
- 변환 도구: Python 3.13.13, NumPy 2.3.4로 수치 변환하고,
  Microsoft DirectXTex `texassemble` 2026.5.8.1(may2026 릴리스)로 조립했다.
  Python과 NumPy는 로컬 Blender 5.2에 포함된 런타임을 사용했다.
- 설정: RGBE를 선형 float32로 복원하고, 공통 노출 −1 EV(RGB × 0.5)를
  적용했다. 원본 최대 RGB 값 75,264가 FP16 최대 유한값 65,504를 넘으므로
  두 환경 맵에 같은 배율을 적용해 하이라이트 잘림을 피했다.
  톤 매핑, 감마 변환, 채널별 정규화는 하지 않았다.
- 각 면의 픽셀을 아래 Direct3D 방향식으로 역투영했다. 픽셀당 균등한
  4 × 4 서브픽셀 위치(중심 대비 각 축 −0.375, −0.125, 0.125, 0.375픽셀)를
  원본에서 bilinear 샘플링한 뒤 평균했다. 경도는 wrap, 위도는 clamp다.
- 임시 float32 RGBA DDS 6장을 아래 순서로 조립했다.
  `texassemble cube -f R16G16B16A16_FLOAT -dx10 -o <출력> <+X> <-X> <+Y> <-Y> <+Z> <-Z>`.
  추가 회전, 좌우 반전, 상하 반전, 밉 생성은 하지 않았다.

### kloofendal_diffuse.dds

- 출처: Poly Haven, [Kloofendal 48d Partly Cloudy](https://polyhaven.com/a/kloofendal_48d_partly_cloudy).
- 작가: Greg Zaal.
- 라이선스: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/),
  [Poly Haven 라이선스 안내](https://polyhaven.com/license).
- 원본 파일명: `kloofendal_48d_partly_cloudy_4k.hdr`.
- [원본 다운로드](https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_4k.hdr).
- 원본 MD5: `afb5e5f0c035d24567e8d778dd35c752`.
- 변환 도구: Python 3.13.13, NumPy 2.3.4,
  DirectXTex `texassemble` 2026.5.8.1.
- 위 t2와 동일한 원본, 노출 −1 EV, 좌표 방향을 사용했다.
  이미 축소한 specular 맵을 흐리는 대신 원본 HDRI에서 직접 적분했다.
- 원본의 4 × 4 픽셀마다 정확한 구면 입체각을 곱한 RGB 에너지를 합쳐
  1024 × 512 적분 셀을 만들었다. 각 셀은 중심 방향을 대표 방향으로 쓴다.
  위도 구간의 입체각은 `Δφ × (cos θ0 − cos θ1)`다.
  이 방식으로 작은 태양도 저해상도 입력의 점 샘플링에서 사라지지 않는다.
- 각 출력 텍셀의 단위 법선 n마다 전체 적분 셀에 대해
  `max(dot(n, ω), 0)`를 곱해 합하고 π로 나누었다. Gaussian blur가 아니다.
- 저장 값은 다음과 같은 정규화된 irradiance, 즉 Lambert diffuse의 `E/π`다.
  이 값을 albedo에 곱해 사용할 때 π로 다시 나누지 않는다.

```text
L(ω) = 원본 선형 HDR RGB × 0.5
D(n) = (1 / π) ∫sphere L(ω) max(n · ω, 0) dω
     ≈ (1 / π) Σcells [max(n · ωcell, 0) × Σpixels_in_cell Lpixel Δωpixel]
```

- 결과를 32 × 32 float32 RGBA 면 6장으로 만든 뒤,
  t2와 같은 `texassemble cube -f R16G16B16A16_FLOAT -dx10` 설정으로 조립했다.
  alpha는 1이며 밉을 만들지 않았다.

### cobblestone_albedo.png

- 출처: Poly Haven, [Cobblestone Floor 01](https://polyhaven.com/a/cobblestone_floor_01).
- 작가: Rob Tuytel.
- 라이선스: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/),
  [Poly Haven 라이선스 안내](https://polyhaven.com/license).
- 원본 파일명: `cobblestone_floor_01_diff_1k.png`.
- [원본 다운로드](https://dl.polyhaven.org/file/ph-assets/Textures/png/1k/cobblestone_floor_01/cobblestone_floor_01_diff_1k.png).
- 원본: 1024 × 1024, 채널당 16비트 PNG Diffuse 컬러 맵.
  AO나 normal, roughness 맵은 합성하지 않았다.
- 원본 MD5: `0a77ce09ef3503560cb7011c5988e668`.
  Poly Haven 파일 API의 값과 다운로드 파일이 일치했다.
- 변환 도구: DirectXTex `texconv` 2026.5.8.1, Windows WIC PNG 코덱.
- 설정: `texconv -ft png -f R8G8B8A8_UNORM -m 1 -o <임시 출력 폴더> <원본>`.
  출력 파일을 `cobblestone_albedo.png`로 이름을 바꾸었다.
  해상도, 원래의 타일 경계, RGB 컬러 인코딩을 유지하며 채널당 8비트로
  줄였다. alpha는 모두 255다. 추가 조명이나 노출을 적용하지 않았다.
- RGB는 sRGB 컬러 데이터다. 선형 조명 계산에 쓸 때는 sRGB SRV로 읽거나
  동등한 sRGB → linear 처리를 한 번 적용한다. 타일링에는 wrap 샘플러를 쓴다.

## 큐브맵 면 순서와 방향

두 DDS 모두 DX10 확장 헤더를 가진 표준 DDS다.
`dxgiFormat = 10`(R16G16B16A16_FLOAT), `resourceDimension = 3`(Texture2D),
`miscFlag = 4`(TEXTURECUBE), `arraySize = 1`(큐브 1개)다.
레거시 caps2에도 CUBEMAP과 6면 플래그를 모두 설정했다.
로더가 반환하는 2D 배열 크기와 서브리소스 개수는 각각 6이다.

[Microsoft DDS 면 순서 규칙](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-file-layout-for-cubic-environment-maps)을 따른다.
Direct3D 왼손 좌표계에서 +X는 오른쪽, +Y는 위, +Z는 앞이다.
각 면은 큐브 중심에서 바깥을 보는 카메라이고 이미지의 첫 행은 위쪽이다.

| 저장 인덱스 | 면 / 카메라 전방 | 화면 오른쪽 | 화면 위쪽 | 정규화 전 샘플 방향 |
| ---: | --- | --- | --- | --- |
| 0 | +X | −Z | +Y | (1, −t, −s) |
| 1 | −X | +Z | +Y | (−1, −t, s) |
| 2 | +Y | +X | −Z | (s, 1, t) |
| 3 | −Y | +X | +Z | (s, −1, −t) |
| 4 | +Z | +X | +Y | (s, −t, 1) |
| 5 | −Z | −X | +Y | (−s, −t, −1) |

```text
s = 2u − 1, t = 2v − 1
u = (x + 0.5) / 면 너비, v = (y + 0.5) / 면 높이
d = normalize(면의 샘플 방향)

원본 equirectangular UV:
U = fract(0.5 + atan2(d.x, d.z) / (2π))
V = acos(clamp(d.y, −1, 1)) / π
```

원본 가로 중앙은 +Z, 3/4 지점은 +X, 1/4 지점은 −X,
가로 양 끝의 이음매는 −Z, 위쪽 극은 +Y다. 두 환경 맵에 같은 기준을
사용했으므로 sky, reflection, diffuse 사이에 별도 방향 보정이 필요 없다.

## 검증 결과

- DirectXTex `texdiag info`로 두 DDS의 Cube 형식, 6면, 해상도,
  FP16 RGBA, mipLevels 1을 확인했다. 별도 헤더 검사로 파일 길이,
  DX10 헤더, cubemap 플래그, 모든 alpha 1을 확인했다.
- Microsoft [DirectXTK12 DDSTextureLoader](https://github.com/microsoft/DirectXTK12/wiki/DDSTextureLoader)의
  원본 `LoadDDSTextureFromFile`을 별도 임시 C++ 실행 파일에서 호출했다.
  두 파일 모두 성공했고 D3D12 리소스, cube=true, 올바른 해상도,
  format 10, mip 1, 서브리소스 6개를 반환했다.
- 로더의 읽기 경로는 수정하지 않았다. 검증기는 업로드와 배리어를 직접
  수행했다. 사용하지 않는 ResourceUploadBatch 연결 함수에는 호출 시 실패하는
  임시 링크용 함수를 두었고, 해당 경로는 호출되지 않았다.
- D3D12 WARP에서 실제 TextureCube SRV를 생성했다. 생성 스크립트의
  방향식을 복사하는 대신 면별 전방·위쪽 벡터와 `cross(up, forward)`로
  왼손 카메라의 오른쪽 벡터를 만들고, 모든 텍셀 중심 방향을 계산했다.
  compute shader의 point sampler와 `SampleLevel(..., 0)`으로 읽은 결과를
  CPU의 DDS 원시 픽셀과 비교했다. specular 1,572,864개, diffuse 6,144개
  텍셀의 RGBA가 모두 정확히 일치했다(불일치 0, 최대 오차 0).
  따라서 실제 Direct3D 면 선택 및 면 내부 방향과 저장 순서가 일치한다.
- 두 DDS의 모든 RGB는 유한하고 음수가 없다. 최종 specular 최대값은
  36,832여서 FP16 범위 잘림이 없다. float32 변환 결과 대비 FP16 최대
  상대 양자화 오차는 specular 0.04881%, diffuse 0.04827%였다.
- diffuse의 면마다 3개, 총 18개 법선에서 원본 4096 × 2048의 모든 픽셀로
  입체각 가중 적분을 다시 수행했다. 최종 DDS 대비 채널별 상대 오차는
  최대 0.07670%, 평균 0.02212%였다(적분 근사와 FP16 양자화 포함).
- 환경 맵의 십자 프리뷰에서 하늘과 지면, 옆면 연결 방향을 확인했다.
  바닥 PNG를 2 × 2로 반복한 프리뷰에서 타일 경계를 시각적으로 확인했다.
  이 프리뷰의 톤 매핑은 확인용 이미지에만 적용했다.
- 프로젝트 코드에 연결한 실제 레이 씬 실행은 이번 작업 범위에 포함하지 않았다.
  소스, 셰이더, 프로젝트 파일, 루트 README는 이 자산 작업에서 수정하지 않았다.
  변환·검증 스크립트, 검증 실행 파일, 다운로드 원본은 저장소 밖 임시 폴더에 두었다.

## 최종 파일 SHA-256

```text
kloofendal_specular.dds
3b4dc9457bcb4b615468e48da261cfcc884f2357596ab3b6f24dd69f379bd117

kloofendal_diffuse.dds
f0415d3cd0d4e2236b89fcf64fddff58d6940ff5a42f1bb7eb282b34f4e9664f

cobblestone_albedo.png
9483be9023d98f36610f80d5addb5640e461c9b9d7ed69bcef946b70a1daae34
```
