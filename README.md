# ComputerGraphicsDX12

Part 2(DX11)에서 배운 내용을 DX12로 옮기며 학습하고, 결과를 렌더러 하나로 모아 포트폴리오로 쓰는 프로젝트다.

사용자, Claude Code, Codex가 같은 작업 트리를 쓴다. 이 문서는 셋이 공유하는 프로젝트 문맥이다.

| 문서 | 역할 |
| --- | --- |
| `README.md` | 공용 문서. 프로젝트 상태, 인수인계, 빌드, 검증, 설계 규칙을 기록한다. |
| `AGENTS.md` | Codex가 관리하는 전용 지침. 공용 규칙은 README를 참조한다. |
| `CLAUDE.md` | Claude Code가 관리하는 전용 지침. 공용 규칙은 README를 참조한다. |

## 현재 상태

작업을 이어받는 에이전트는 여기부터 본다. 단계를 끝낸 에이전트가 이 절을 갱신한다.

- **완료:** Hello Triangle. 창, 디바이스, 스왑체인, 커맨드, PSO, 펜스, 프레임 루프를 `src/main.cpp`에 구현했다.
- **진행 중:** 사용자가 Hello Triangle의 흐름을 학습한다. 사용자가 다음 단계를 시작하자고 할 때까지 기능을 추가하지 않는다.
- **다음 목표:** Part 2의 레이 씬을 DX12로 렌더링한다. 필요 범위는 `1차 목표: 레이 씬` 절에 있다.

### 작업 인수인계

단계가 끝나기 전에 Claude Code와 Codex가 교대하면, 넘기는 쪽이 아래 세 항목을 갱신한다. 변경한 파일에는 경로와 변경 요약을, 검증 결과에는 실행한 절차와 결과를, 다음 할 일에는 이어서 할 작업과 사용자 결정이 필요한 항목을 적는다. 실행하지 않은 검증은 `미실행`으로 적는다. 이어받는 쪽은 작업 전에 최신 기록과 관련 파일을 확인한다.

**최신 기록**

- 변경한 파일 — `README.md` 에 레이 씬을 첫 구현 목표로 확정하고 필요 범위를 적었다. `CLAUDE.md` 에서 `@AGENTS.md` 참조를 제거했다. `.gitignore` 를 추가하고 git을 초기화해 비공개 GitHub 저장소로 publish했다(`1fe223c`). 그 뒤 문서 변경은 `docs/shared-context` 브랜치에 커밋했고 아직 push하지 않았다.
- 검증 결과 — publish 때 로컬과 원격 `main` 의 커밋이 같은 것을 확인했다. Codex 리뷰에서 Debug·Release 빌드와 Debug 실행이 통과했다고 보고됐다. Claude Code는 이후 문서만 변경했고 빌드와 앱 실행은 `미실행`.
- 다음 할 일 — 학습은 Hello Triangle에서 이어간다. 레이 씬 구현은 사용자의 시작 요청을 기다린다. 사용자 결정 필요: 전체 계획의 판단 1 기준 조정, 레이 씬의 포트폴리오 포함 여부.

## 기준 자료

- 전체 계획: `C:\Jaemin\StudyAndJob\study\part3-dx12-internship-plan.md`. 범위, 일정, 설계 결정은 이 문서를 따른다. 첫 구현 목표만 아래 `1차 목표: 레이 씬` 절이 우선한다.
- Part 2 예제: `C:\Jaemin\ComputerGraphicsDX11_Part2\05_08_RealtimePipeline\08_ShaderToys_Step7_Shadertoy`
- 레이 씬 코드(위 폴더): `RayScenePixelShader.hlsl`, `BasicVertexShader.hlsl`, `CubeMappingVertexShader.hlsl`, `CubeMappingPixelShader.hlsl`, 장면 구성과 드로우 순서는 `ExampleApp.cpp`
- 레이 씬 자산(위 폴더): 큐브맵 `.setup\ShaderToyCubemap\StPeters_diffuse.dds`, `StPeters_specular.dds`, 바닥 텍스처 `wall.jpg`

## 1차 목표: 레이 씬

전체 계획의 "텍스처 메시와 Blinn-Phong 먼저" 대신 이 장면을 첫 구현 목표로 한다. 업로드, 링 버퍼, 루트 시그니처, PSO, 텍스처를 모두 거치고, Part 2 실행 결과를 정답 화면으로 쓸 수 있기 때문이다.

전체 화면 패스가 아니다. 스카이 큐브맵을 깊이 없이 먼저 그리고, 구 6개와 바닥을 메시로 래스터화한다. 픽셀 셰이더는 1차 가시성을 래스터화 결과에서 받고 반사와 굴절 광선만 추적한다.

Hello Triangle에 더해야 하는 기능은 다음과 같다. 범위만 적은 것이고, 구현 설계는 `작업 방식`에 따라 사용자가 정한다.

- **드로우별 상수:** `b0`(행렬)과 `b1`(`objectIndex`를 포함한 장면 데이터)이 드로우 7개마다 바뀐다. GPU는 커맨드를 나중에 실행하므로 드로우마다 다른 메모리를 써야 한다. 프레임이 1개만 돌아도 마찬가지다.
- **깊이 버퍼와 인덱스 메시:** 구와 바닥 사각형을 그린다.
- **PSO 2개:** 스카이 패스는 깊이를 쓰지 않는다.
- **텍스처:** `t1`, `t2` 큐브맵(DDS), `t3` 바닥(JPG), 샘플러 `s0`. 전부 `SampleLevel(..., 0)`로 읽어서 밉 생성은 필요 없다.

ImGui와 리사이즈는 이 단계의 범위가 아니다. 정답 화면과 비교할 때는 카메라와 조명이 시간에 따라 움직이므로 같은 시각으로 맞춘다.

이 장면은 Part 2 실습과 Shadertoy 원본(`fsGcW3`)에서 파생됐다. 포트폴리오 제출물에 넣을지는 `작업 방식`의 규칙과 원본 라이선스를 확인한 뒤 사용자가 정한다.

## 작업 방식

- Hello Triangle까지만 에이전트가 직접 구현했다. 이후 기능은 기존 흐름을 함께 읽고, 질문과 단계별 힌트로 사용자가 핵심 설계를 정한 뒤 에이전트가 구현하고 검증한다.
- 사용자가 코드 없이 설명할 수 있어야 하는 구역은 프레임 자원 수명, 배리어와 자원 흐름, 루트 시그니처와 디스크립터, PBR과 그림자 좌표 변환, 측정 실험이다. 이 구역의 설계를 에이전트가 먼저 확정하지 않는다.
- DX11 코드는 줄 단위로 포팅하지 않고 정답 화면의 기준으로 읽는다. 한 번에 기능 하나만 추가한다.
- 계획상 결정: FXC와 SM 5.1, 루트 시그니처 1개, 셰이더 가시 힙 1개, 패스별 PSO, 프레임별 선형 링 버퍼. 렌더 그래프, 메모리 할당 라이브러리, DXC, 바인드리스, 비동기 로딩은 쓰지 않는다.
- 포트폴리오 제출물에 강의 코드와 실습 정답을 넣지 않는다. Part 2 파일을 이 프로젝트에 복사하기 전에 사용자에게 확인한다.

## 프로젝트 구조

Windows C++17 프로젝트다. `src/main.cpp` 에 Win32 진입점과 렌더러가 있다. `HelloTriangleApp::Initialize()` 에서 초기화 순서를, `Render()` 에서 한 프레임의 GPU 작업을 확인할 수 있다. `shaders/` 에는 정점·픽셀 셰이더와 공용 선언 파일인 `Common.hlsli` 가 있다. 별도 테스트나 에셋 디렉터리는 없다. `x64/` 와 `Computer.6f1c3a52/` 의 빌드 산출물은 버전 관리에서 제외한다.

## 빌드와 실행

Visual Studio 18, MSVC v145, Windows SDK가 필요하다. 프로젝트 루트에서 Developer PowerShell을 열고 실행한다.

```powershell
MSBuild.exe .\ComputerGraphicsDX12.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
.\x64\Debug\ComputerGraphicsDX12.exe
```

Release를 쓰려면 두 명령의 `Debug` 를 `Release` 로 바꾼다. 일반 PowerShell에서는 `& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'` 로 MSBuild를 호출할 수 있다.

MSBuild는 FXC로 HLSL을 Shader Model 5.1로 컴파일하고 `.cso` 를 실행 파일 옆에 둔다. 새 `.hlsl` 은 `ComputerGraphicsDX12.vcxproj` 에 `FxCompile` 과 셰이더 단계에 맞는 `ShaderType` 을 등록하고 진입점은 `main` 으로 둔다. 공용 `.hlsli` 는 `None` 으로 등록한다.

## 코드 스타일과 검증

- C++은 `.clang-format` 을 따른다. 들여쓰기는 공백 4칸, 줄 길이는 80자다. `clang-format -i --style=file src/main.cpp` 로 정리한다. PATH에 없으면 `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe` 를 사용한다.
- 타입과 함수는 `PascalCase`, 지역 변수는 `camelCase` 를 쓴다. 멤버에는 `m_`, 상수에는 `k` 접두사를 붙인다. 초기화 순서를 유지한다.
- 자동 테스트와 커버리지 기준은 없다. Debug 빌드를 실행해 색상 삼각형이 표시되고 창이 정상적으로 닫히는지 확인한다. 디버그 레이어를 켠 상태에서 stderr에 `[D3D12 WARNING]` 과 `[D3D12 ERROR]` 가 없는지 확인한다.
- 커밋 제목은 짧은 명령형으로 쓴다. PR에는 변경 내용, 빌드와 실행 확인 결과, 관련 이슈가 있으면 링크를 적는다. 화면에 보이는 변경에는 스크린샷을 붙인다.

## 설계 메모

- **프레임 동기화:** 진행 중인 프레임은 항상 1개다. `Render()` 끝의 `WaitForGpu()`가 GPU 완료를 기다리기 때문에 커맨드 할당자 하나를 매 프레임 `Reset()`할 수 있다. 여러 프레임을 동시에 돌리려면 할당자와 펜스 값을 프레임별로 나눠야 한다.
- **창과 스왑체인:** 창 크기는 고정이고 리사이즈 경로가 없다. 그래서 Alt+Enter도 막아 두었다.
- **헬퍼 헤더:** `d3dx12.h`는 Windows SDK에 없어서 쓰지 않고, D3D12 구조체를 직접 채운다. 계획에 있는 `directx-headers`는 필요해질 때 추가한다.
- **디버그 레이어:** Debug 빌드에서만 켜진다. `OnD3D12Message`가 경고 이상을 콘솔로 출력한다.
- **어댑터 선택:** 이 PC에는 NVIDIA, AMD 내장, Parsec 가상 어댑터가 함께 있어서 `DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`로 고른다. 선택된 어댑터 이름이 stdout에 찍힌다.
