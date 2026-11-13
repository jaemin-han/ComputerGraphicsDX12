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

- **완료:** Hello Triangle. 창, 디바이스, 스왑체인, 커맨드, PSO, 펜스, 프레임 루프를 `src/main.cpp`에 구현했다. 사용자가 이 흐름을 학습했고, 코드 없이 한 프레임을 설명하는 확인까지 마쳤다.
- **완료:** 프레임 2개 동시 진행. 사용자가 할당자와 "그 프레임이 기다릴 펜스 값"을 프레임별로, 큐, 리스트, 펜스, 이벤트, 카운터를 앱 전체에 하나로 정했다. 원래 계획의 11/13 항목을 앞당겼다.
- **완료:** 엔진과 메시 분리. 사용자가 "엔진이 메시를 갖고 직접 만들고 그린다, 메시는 데이터만 갖는다"로 정했다. 엔진은 싱글톤 패턴 없이 `main()` 에서 하나 만든다.
- **완료:** 드로우별 상수 b0. 사용자가 "프레임마다 256B × 3 칸, i번째 드로우는 i번 칸, 펜스 대기 뒤 재사용"과 "b0는 루트 CBV"를 정했다. 같은 삼각형을 b0만 바꿔 세 번 그린다.
- **완료:** 인덱스 버퍼. `Mesh` 에 인덱스 버퍼를 더하고, 같은 삼각형 3개를 `DrawIndexedInstanced` 로 그린다. 화면은 이전 단계와 같다.
- **완료:** 메시와 프레임 자원 분리(리팩터링). 사용자 요청으로 `Engine.cpp` 의 메시 함수를 떼어 냈다. `Mesh` 가 자기 버퍼를 만들고 그리며(`Mesh::Create`, `Mesh::Draw`), 드로우별 상수 칸 쓰기는 메시가 아니라 `FrameResource::WriteConstants` 가 맡는다. 화면은 이전 단계와 같다.
- **완료:** 깊이 없는 3D 장면. Part 2 배치대로 구 6개와 바닥을 궤도 카메라로 그리고 노멀을 색으로 출력한다. 깊이 버퍼가 없어서 마지막에 그린 바닥이 구의 아랫부분을 덮는다. 다음 단계와 비교하려고 일부러 남긴 상태다.
- **완료:** 깊이 버퍼. 사용자가 "깊이 버퍼는 GPU만 읽고 쓰고 프레임은 큐에서 차례로 실행되므로 엔진에 하나"로 정했다. 바닥이 구를 덮던 가림이 바로잡혔다.
- **진행 중:** 텍스처. ① vcpkg로 DirectXTex 연결(완료, 화면 변화 없음) ② 텍스처 업로드(업로드 버퍼 → 복사 → 배리어 → 대기, 사용자 설계 중) ③ SRV 힙과 디스크립터 테이블로 바닥에 텍스처 ④ 스카이 패스(두 번째 PSO) 순서로 나눠 진행한다. 텍스처 파일은 Part 2 자산 대신 사용자 요청으로 Codex가 CC0 자산(Poly Haven)을 받아 `assets/textures/` 에 두었다. 출처, 라이선스, 변환 방법은 `assets/README.md` 에 있다.
- **다음 목표:** Part 2의 레이 씬을 DX12로 렌더링한다. 필요 범위는 `1차 목표: 레이 씬` 절에 있다.

### 작업 인수인계

단계가 끝나기 전에 Claude Code와 Codex가 교대하면, 넘기는 쪽이 아래 세 항목을 갱신한다. 변경한 파일에는 경로와 변경 요약을, 검증 결과에는 실행한 절차와 결과를, 다음 할 일에는 이어서 할 작업과 사용자 결정이 필요한 항목을 적는다. 실행하지 않은 검증은 `미실행`으로 적는다. 이어받는 쪽은 작업 전에 최신 기록과 관련 파일을 확인한다.

**최신 기록**

- 변경한 파일 — `vcpkg.json`(DirectXTex, 기본 기능 끄고 `dx12` 만), `Directory.Build.props`(Part 3과 같은 vcpkg 매니페스트 설정, 설치 위치는 `vcpkg_installed/`), `Directory.Build.targets`(vcpkg를 못 찾으면 원인을 알려 주는 오류), `.gitignore`(`vcpkg_installed/`)를 더했다. 코드 변경은 없다. 이전 커밋 `c00c072` 에서 깊이 버퍼를 넣었다.
- 검증 결과 — Claude Code가 Debug 빌드를 확인했다. vcpkg가 `directxtex[core,dx12]` 를 binary cache에서 설치했고(약 15초), `vcpkg_installed/x64-windows` 에 `DirectXTex.h`, `d3dx12.h`, Debug와 Release용 `DirectXTex.lib`(정적 라이브러리)가 생겼다. 빌드 뒤 실행 화면은 깊이 버퍼 단계와 같고 stderr에 D3D12 메시지가 없으며 종료 코드 0으로 끝난다. 아직 DirectXTex 함수를 부르지 않아서 링크 확인은 `미실행`(업로드 단계에서 확인한다).
- 다음 할 일 — 텍스처 업로드를 사용자와 설계한다. 질문은 기본 힙 텍스처의 처음 상태, 복사 뒤 배리어(어느 상태에서 어느 상태로), 업로드 버퍼를 해제해도 되는 시점이다. Codex가 `assets/textures/` 에 `kloofendal_specular.dds`(t2, 512² × 6면, RGBA16F), `kloofendal_diffuse.dds`(t1, 32² × 6면, RGBA16F, 코사인 가중 irradiance를 π로 나눈 값), `cobblestone_albedo.png`(t3, 1024², RGBA8, sRGB 색 데이터)를 두었다. Claude Code가 검토했다. SHA-256 3개가 `assets/README.md` 와 일치하고, DDS 헤더(DX10, 큐브 플래그, 6면, 밉 1, 파일 길이)와 PNG 크기가 기록과 같다. 두 큐브맵을 +Y / −X +Z +X −Z / −Y 십자로 펼쳐 지평선과 위·아래 면의 이음매가 이어지는 것도 눈으로 확인했다. 바닥 PNG를 UNORM으로 읽을지 sRGB로 읽을지는 바닥 텍스처 단계에서 정한다. 그다음은 SRV 힙과 디스크립터 테이블, 스카이 패스, 레이 PS 순서다. 레이 씬에서 크기가 다른 b1(512B)이 섞이면 "i번 칸" 대신 지금까지 쓴 바이트 수로 칸을 나눌지 사용자가 정한다. 학습에서 남은 약점은 배리어는 리소스를 받고 Clear와 OMSet은 디스크립터 핸들을 받는 이유, 디스크립터 칸을 덮어쓰거나 리소스를 해제할 때의 영향, 루트 시그니처다. 사용자 결정 필요: 전체 계획의 판단 1 기준 조정, 레이 씬의 포트폴리오 포함 여부, Part 2 자산과 레이 셰이더를 가져올지.

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

ImGui와 리사이즈는 이 단계의 범위가 아니다. 정답 화면과 비교할 때는 카메라와 조명이 시간에 따라 움직이므로 같은 시각으로 맞춘다. 텍스처는 Part 2 자산 대신 CC0 자산(`assets/`)을 쓰므로, 비교는 색이 아니라 구조(가림, 반사와 굴절이 생기는 위치, 그림자 위치)를 기준으로 한다.

이 장면은 Part 2 실습과 Shadertoy 원본(`fsGcW3`)에서 파생됐다. 포트폴리오 제출물에 넣을지는 `작업 방식`의 규칙과 원본 라이선스를 확인한 뒤 사용자가 정한다.

## 작업 방식

- Hello Triangle까지만 에이전트가 직접 구현했다. 이후 기능은 기존 흐름을 함께 읽고, 질문과 단계별 힌트로 사용자가 핵심 설계를 정한 뒤 에이전트가 구현하고 검증한다.
- 사용자가 코드 없이 설명할 수 있어야 하는 구역은 프레임 자원 수명, 배리어와 자원 흐름, 루트 시그니처와 디스크립터, PBR과 그림자 좌표 변환, 측정 실험이다. 이 구역의 설계를 에이전트가 먼저 확정하지 않는다.
- DX11 코드는 줄 단위로 포팅하지 않고 정답 화면의 기준으로 읽는다. 한 번에 기능 하나만 추가한다.
- 계획상 결정: FXC와 SM 5.1, 루트 시그니처 1개, 셰이더 가시 힙 1개, 패스별 PSO, 프레임별 선형 링 버퍼. 렌더 그래프, 메모리 할당 라이브러리, DXC, 바인드리스, 비동기 로딩은 쓰지 않는다.
- 포트폴리오 제출물에 강의 코드와 실습 정답을 넣지 않는다. Part 2 파일을 이 프로젝트에 복사하기 전에 사용자에게 확인한다.

## 프로젝트 구조

Windows C++17 프로젝트다. `src/main.cpp` 에 진입점이, `src/Engine.*` 에 렌더러(`Engine`)가, `src/FrameResource.*` 에 프레임별 할당자와 상수 칸이, `src/Mesh.*` 에 정점, 메시 데이터, 메시 생성과 그리기가, `src/Geometry.*` 에 구와 사각형 생성이, `src/ConstantBuffers.h` 에 셰이더 cbuffer와 같은 배치의 구조체가, `src/Window.*` 에 Win32 창 생성이, `src/Util.*` 에 `Check`, 업로드 버퍼 헬퍼, 셰이더 로더, 디버그 레이어 콜백이 있다. `Engine::Initialize()` 에서 초기화 순서를, `Engine::Render()` 에서 한 프레임의 GPU 작업을 확인할 수 있다. `shaders/` 에는 정점·픽셀 셰이더와 공용 선언 파일인 `Common.hlsli` 가 있다. 별도 테스트나 에셋 디렉터리는 없다. `x64/` 와 `Computer.6f1c3a52/` 의 빌드 산출물은 버전 관리에서 제외한다.

## 빌드와 실행

Visual Studio 18, MSVC v145, Windows SDK, 부트스트랩한 vcpkg가 필요하다. vcpkg는 `VCPKG_ROOT` 환경 변수나 이 프로젝트 옆 폴더(`..\vcpkg`)에서 찾는다. 외부 라이브러리는 `vcpkg.json` 에 적고, 첫 빌드 때 MSBuild가 `vcpkg_installed/` 에 설치한다(`Directory.Build.props`). 프로젝트 루트에서 Developer PowerShell을 열고 실행한다.

```powershell
MSBuild.exe .\ComputerGraphicsDX12.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
.\x64\Debug\ComputerGraphicsDX12.exe
```

Release를 쓰려면 두 명령의 `Debug` 를 `Release` 로 바꾼다. 일반 PowerShell에서는 `& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'` 로 MSBuild를 호출할 수 있다.

MSBuild는 FXC로 HLSL을 Shader Model 5.1로 컴파일하고 `.cso` 를 실행 파일 옆에 둔다. 새 `.hlsl` 은 `ComputerGraphicsDX12.vcxproj` 에 `FxCompile` 과 셰이더 단계에 맞는 `ShaderType` 을 등록하고 진입점은 `main` 으로 둔다. 공용 `.hlsli` 는 `None` 으로 등록한다.

## 코드 스타일과 검증

- C++은 `.clang-format` 을 따른다. 들여쓰기는 공백 4칸, 줄 길이는 80자다. `clang-format -i --style=file src/*.cpp src/*.h` 로 정리한다. PATH에 없으면 `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe` 를 사용한다.
- 타입과 함수는 `PascalCase`, 지역 변수는 `camelCase` 를 쓴다. 멤버에는 `m_`, 상수에는 `k` 접두사를 붙인다. 초기화 순서를 유지한다.
- C++과 HLSL 주석은 한국어로 쓰고, 이 문서처럼 `~한다` 체로 쓴다. API, 타입, 함수 이름은 원문 그대로 둔다. 소스 파일은 BOM 없는 UTF-8이고, MSVC가 시스템 코드 페이지와 상관없이 UTF-8로 읽도록 프로젝트에 `/utf-8` 을 지정했다.
- 자동 테스트와 커버리지 기준은 없다. Debug 빌드를 실행해 현재 단계의 기대 화면(`현재 상태` 의 마지막 완료 항목)이 표시되고 창이 정상적으로 닫히는지 확인한다. 디버그 레이어를 켠 상태에서 stderr에 `[D3D12 WARNING]` 과 `[D3D12 ERROR]` 가 없는지 확인한다.
- 커밋 제목은 짧은 명령형으로 쓴다. PR에는 변경 내용, 빌드와 실행 확인 결과, 관련 이슈가 있으면 링크를 적는다. 화면에 보이는 변경에는 스크린샷을 붙인다.

## 설계 메모

- **파일 구조:** Hello Triangle 코드는 버리지 않고 레이 씬과 이후 기능의 출발점으로 쓴다. 설계 판단이 없는 창 생성과 유틸리티는 `src/Window.*`, `src/Util.*` 로 분리했다. 학습용이라 성능보다 가독성을 우선한다.
- **엔진과 메시:** 처음에는 `Mesh` 를 데이터만 담는 구조체로 두고 `Engine` 이 만들고 그렸다. `Engine.cpp` 가 길어져서 사용자가 메시 함수를 떼어 내기로 했다. 나누는 기준은 함수가 필요로 하는 객체와 다루는 데이터의 수명이다. `Mesh` 는 앱 수명의 정점, 인덱스 버퍼를 갖고 `Create(device, ...)`, `Draw(commandList)` 로 디바이스와 리스트를 인자로 받는다(Part 3의 `Model::Render(context)` 와 같은 모양). 드로우마다 바뀌는 상수는 같은 메시를 여러 번 그릴 때 값이 달라서 메시가 아니라 `FrameResource` 가 칸에 쓴다. 칸에 쓰기(메모리)는 `FrameResource` 가, 그 주소를 몇 번 루트 파라미터에 연결할지(바인딩)는 루트 시그니처를 가진 `Engine` 이 한다. 엔진은 `main()` 에서 하나만 만들고 아무 데서나 꺼내 쓸 일이 없어서 싱글톤 패턴은 쓰지 않는다. 루트 시그니처와 PSO 생성은 스카이 PSO를 넣을 때, 텍스처 로딩은 텍스처 단계에서 따로 떼어 낸다. 레이 씬이 다 들어온 뒤 `Render()` 가 다시 길어지면 엔진(디바이스, 큐, 프레임)과 장면(메시, PSO, 드로우 순서)을 나누는 것을 검토한다.
- **프레임 동기화:** 진행 중인 프레임은 최대 `kFrameCount`(2)개다. 프레임마다 `FrameResource` 에 커맨드 할당자와 그 프레임을 제출할 때 `Signal` 한 펜스 값을 둔다. `Render()` 는 시작할 때 자기 칸의 펜스 값까지만 기다린 뒤 할당자를 `Reset()` 한다. 큐, 커맨드 리스트, 펜스, 이벤트, 카운터(`m_fenceCounter`)는 하나다. 리스트는 제출 직후 다른 할당자로 `Reset()` 할 수 있어서 하나로 충분하다. 백버퍼 수(`kBackBufferCount`)는 스왑체인이 정하는 값이라 `kFrameCount` 와 따로 둔다. 프레임 중에 갱신하는 GPU 메모리(레이 씬의 상수 링 버퍼 등)도 프레임별로 나눠야 한다.
- **드로우별 상수:** GPU는 커맨드 리스트를 나중에 실행하면서 기록된 주소를 그때 읽는다. 그래서 드로우마다 다른 칸을 써야 한다. DX11에서는 `Map(WRITE_DISCARD)` 때 드라이버가 새 메모리를 주던 일이다. 칸은 `FrameResource` 의 업로드 버퍼(256B × `kMaxDrawsPerFrame`)에 있고, i번째 드로우가 `FrameResource::WriteConstants(i, ...)` 로 i번 칸을 쓴다. 상수 버퍼 주소는 256바이트 배수여야 한다. 프레임마다 버퍼를 따로 두는 이유는 값이 바뀌어서가 아니라, 다음 프레임을 기록하는 동안 GPU가 이전 프레임의 칸을 아직 읽고 있을 수 있어서다. 프레임 시작의 펜스 대기를 지나면 그 프레임의 칸을 다시 쓴다.
- **루트 시그니처:** 셰이더가 받는 인자 목록이다. DX11에서 단계별 슬롯 판을 드라이버가 맞춰 주던 일을 직접 선언한다. 파라미터 0은 VS의 b0이고 루트 CBV다. 드로우마다 바뀌는 값이라, 디스크립터 테이블처럼 힙의 view를 드로우마다 만들고 그 칸의 수명까지 관리하는 대신 주소만 넘긴다. 텍스처(t1~t3)는 view가 필요하므로 레이 씬에서 디스크립터 테이블로 넣는다.
- **깊이 버퍼:** 엔진에 하나다. 깊이 버퍼는 GPU만 읽고 쓰고, 프레임들의 명령은 한 큐에서 차례로 실행되므로 다음 프레임의 클리어가 이전 프레임의 드로우와 겹치지 않는다. CPU가 쓰고 GPU가 나중에 읽는 상수 버퍼와 다른 점이다. 백버퍼가 여러 장인 이유(화면 출력이 한 장을 읽고 있음)도 깊이 버퍼에는 없다. 형식은 `D32_FLOAT` 이고 `DEPTH_WRITE` 상태로 만들어 그대로 두므로 배리어가 없다. 셰이더가 깊이를 읽는 패스(그림자 맵 등)가 생기면 배리어가 필요하다. RTV처럼 Clear와 OMSet은 리소스가 아니라 DSV 힙의 칸(디스크립터)을 받는다.
- **창과 스왑체인:** 창 크기는 고정이고 리사이즈 경로가 없다. 그래서 Alt+Enter도 막아 두었다.
- **외부 라이브러리:** 텍스처 파일(DDS, JPG, PNG) 해석은 vcpkg의 DirectXTex(`dx12` 기능)에 맡긴다. 사용자가 DirectXTK12 대신 골랐다. TK12의 `ResourceUploadBatch` 는 업로드, 복사, 상태 전환까지 숨기지만, DirectXTex는 CPU 이미지까지만 만들어 주므로 업로드 버퍼, `CopyTextureRegion`, 배리어, 완료 대기는 이 프로젝트 코드에 남는다.
- **헬퍼 헤더:** DirectXTex의 `dx12` 기능이 `directx-headers`(`d3dx12.h`)를 함께 설치하지만, D3D12 구조체는 지금처럼 직접 채운다.
- **디버그 레이어:** Debug 빌드에서만 켜진다. `OnD3D12Message`가 경고 이상을 콘솔로 출력한다.
- **어댑터 선택:** 이 PC에는 NVIDIA, AMD 내장, Parsec 가상 어댑터가 함께 있어서 `DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`로 고른다. 선택된 어댑터 이름이 stdout에 찍힌다.
