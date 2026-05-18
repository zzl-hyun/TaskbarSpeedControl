# Taskbar Auto-Hide Speed Controller

**Windows 10/11에서 작업표시줄 자동 숨기기 애니메이션 속도를 네이티브 훅으로 가속합니다.**

> 🚀 IAT 패칭 + 정밀 타이밍 조작 = 매끄럽고 제어 가능한 애니메이션 속도 조절

## ✨ 주요 기능

- **🎯 애니메이션 속도 조절** — Show/Hide 배속을 50% ~ 500%로 사용자 정의
- **📊 프레임 레이트 제어** — 프레임 레이트 캡(1~240 FPS)으로 부드러운 애니메이션
- **🔧 정밀 감지** — Windows API 훅(SetWindowPos, MoveWindow, GetTickCount, Sleep)으로 슬라이드 감지
- **⚡ 자동시작 등록** — 시스템 부팅 시 자동 로드 (옵션)
- **📝 진단 로깅** — 선택적 hook.log 트레이싱
- **🔐 안전한 주입** — 비침습 DLL 주입, 자동 정리

## 요구사항

- **Windows 10/11** (x64 전용)
- **.NET 9.0 Runtime** 이상
- **Visual Studio 2022 Community** 또는 **BuildTools** (C++ 워크로드 포함, 빌드 시에만)

## 설치

### 옵션 1: 포터블 (설정 프로그램 불필요)

1. 최신 릴리스 다운로드 또는 소스에서 빌드
2. 임의 폴더에 압축 해제
3. `TaskbarAutoHideSpeed.exe` 실행
4. 필요시 "Run at Startup" 체크하여 자동시작 등록

### 옵션 2: 소스에서 빌드

아래 [빌드 섹션](#빌드) 참조

## 사용법

### 기본 사용

1. **앱 실행** — `TaskbarAutoHideSpeed.exe` 시작
2. **슬라이더 조정**:
   - **Show Speedup**: 작업표시줄 나타나는 속도 (기본값 250%)
   - **Hide Speedup**: 작업표시줄 숨겨지는 속도 (기본값 250%)
   - **Frame Rate**: 애니메이션 프레임 레이트 상한 (기본값 90 Hz)
3. **Apply 클릭** — 훅 설치 및 설정 적용
4. **테스트** — 마우스를 화면 가장자리로 이동하여 작업표시줄 자동숨김 트리거

### 시작 프로그램 등록

- **"Run at Startup"** 체크 → Windows 부팅 시 자동 시작
- **"Start Minimized"** 체크 → 숨겨진 상태로 시작

### 비활성화

- **"Restore Defaults"** 클릭 — 훅 제거 및 기본 속도 복구
- 또는 앱 종료 (Explorer 재시작 시 훅 정리)

## 설정 파일

설정은 다음 위치에 저장됩니다:
```
%APPDATA%\TaskbarAutoHideSpeed\settings.json
```

예시:
```json
{
  "ShowSpeedup": 500,
  "HideSpeedup": 500,
  "FrameRate": 90,
  "RunAtStartup": true,
  "StartMinimized": false
}
```

직접 JSON 파일을 수정 후 Apply를 다시 클릭하면 반영됩니다.

## 문제 해결

### 애니메이션 속도가 변하지 않음

1. 설정 조정 후 **Apply** 클릭 확인
2. 마우스를 화면 모서리로 이동하여 자동숨김 트리거 (클릭 금지)
3. Task Manager에서 `explorer.exe` 프로세스 실행 중 확인
4. "Restore Defaults" 클릭 후 다시 적용 시도

### 특정 작업표시줄 동작에는 영향 없음

- 최적화 대상: **자동숨김 show/hide 애니메이션**
- 미지원: 창 미리보기 등 다른 전환 효과

### 앱이 시작되지 않음

- **.NET 9.0 Runtime** 설치 확인: https://dotnet.microsoft.com/download
- 명령 프롬프트(관리자)에서 오류 확인:
  ```powershell
  Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\startup.log"
  ```

### 상세 로깅 활성화

환경 변수를 설정 후 앱 실행:
```powershell
$env:TASKBAR_AUTOHIDE_TRACE=1
# 그 후 앱 실행, 다음에서 로그 확인:
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\hook.log"
```

로그 항목 예시:
```
Taskbar slide detected: show speedup=500 oldVisible=5120 newVisible=23040
Taskbar slide detected: hide speedup=500 oldVisible=122880 newVisible=102400
AttemptTaskbarViewHook: checking module
...
```

## 로그 위치

| 로그 파일 | 위치 | 목적 |
|---------|------|------|
| startup.log | `%APPDATA%\TaskbarAutoHideSpeed\` | 앱 초기화, 네이티브 훅 호출 |
| hook.log | `%APPDATA%\TaskbarAutoHideSpeed\` | 네이티브 훅 진단 (TASKBAR_AUTOHIDE_TRACE=1 필요) |

## 사용법

1. 앱 실행 → 설정 UI 표시
2. **Show animation speedup** — 나타나는 애니메이션 속도 (250 = 2.5배 빠름)
3. **Hide animation speedup** — 사라지는 애니메이션 속도
4. **Animation frame rate** — 프레임 레이트 (기본값 90 FPS)
5. **Run at startup** — 활성화하면 시작 프로그램에 등록
6. **Apply** 클릭 → 설정 저장 및 hook 적용
7. 창을 닫으면 트레이로 최소화

## 기술 구조

- **C# WinForms UI** (`src/TaskbarAutoHideSpeed/`) — 설정 관리 및 사용자 인터페이스
- **C++ Native DLL** (`src/TaskbarAutoHideHook/`) — Window animation 제어 및 hook 설치

### 동작 흐름

1. C# 앱 시작 → 설정 파일(`%AppData%\TaskbarAutoHideSpeed\settings.json`) 로드
2. UI에서 사용자 설정 변경 후 Apply → `SettingsStore`에 저장
3. `NativeBridge`가 `TaskbarAutoHideHook.dll` 로드 및 `TaskbarAutoHideHook_Initialize()` 호출
4. 네이티브 DLL이 설정 파일 읽기 → window animation hook 설치
5. `Shell_TrayWnd` (taskbar 윈도우) 메시지 인터셉트 및 animation duration 제어

## 설정 저장 위치

- **파일**: `%AppData%\TaskbarAutoHideSpeed\settings.json`
- **예시**:
  ```json
  {
    "ShowSpeedup": 250,
    "HideSpeedup": 250,
    "FrameRate": 90,
    "RunAtStartup": true,
    "StartMinimized": false
  }
  ```

## 시작 프로그램 등록

활성화 시 다음 레지스트리 키에 추가됩니다:
```
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
  TaskbarAutoHideSpeed → "C:\path\to\TaskbarAutoHideSpeed.exe" --autostart
```

`--autostart` 플래그로 시작하면 UI 없이 백그라운드에서 실행됩니다.

## 라이선스

MIT License — 자유롭게 수정 및 배포 가능합니다.

## 문제 해결

- **"Native hook DLL load failed"** 메시지:
  - `TaskbarAutoHideHook.dll`이 exe와 같은 폴더에 있는지 확인
  - 앱을 관리자 권한으로 실행 (일부 환경에서 필요)

- **설정이 적용되지 않음**:
  - Apply 버튼 클릭 후 taskbar를 hide/show (자동 숨기기 활성화 필요)
  - 설정 파일 위치 확인: `%AppData%\TaskbarAutoHideSpeed\settings.json`

## 추가 정보

- 이 도구는 Windhawk mod의 동작 원리를 참고하여 clean-room으로 독립적으로 구현되었습니다
- 실제 animation 수정은 Windows Composition/window hooking을 통해 수행됩니다
- Windows 11 환경에서 테스트되었습니다 (Windows 10 호환성 미확인)
