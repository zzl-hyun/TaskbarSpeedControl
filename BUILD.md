# Build Guide

이 가이드는 Taskbar Auto-Hide Speed Controller 프로젝트를 빌드하는 방법을 설명합니다.

## 요구사항

### 개발 환경
- **Visual Studio 2022 Community** 또는 **BuildTools** (C++ 데스크톱 개발 워크로드)
- **.NET 9.0 SDK** 이상
- **Windows 10/11** (x64)

### 설치 검증

```powershell
# .NET SDK 확인
dotnet --version

# Visual Studio 설치 위치 확인
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -all
```

## 빌드 방법

### 옵션 1: 자동 빌드 스크립트 (권장)

```powershell
cd <프로젝트 폴더>
.\build.ps1 -Configuration Release -Platform x64
```

**출력:**
- `src/TaskbarAutoHideSpeed/bin/x64/Release/net9.0-windows/TaskbarAutoHideSpeed.exe`
- `src/TaskbarAutoHideSpeed/bin/x64/Release/net9.0-windows/TaskbarAutoHideHook.dll`

### 옵션 2: 수동 빌드

#### 1단계: C# 앱 빌드

```powershell
msbuild src\TaskbarAutoHideSpeed\TaskbarAutoHideSpeed.csproj `
  /p:Configuration=Release /p:Platform=x64
```

**출력:**
```
src/TaskbarAutoHideSpeed/bin/x64/Release/net9.0-windows/TaskbarAutoHideSpeed.exe
```

#### 2단계: C++ 네이티브 DLL 빌드

```powershell
msbuild src\TaskbarAutoHideHook\TaskbarAutoHideHook.vcxproj `
  /p:Configuration=Release /p:Platform=x64
```

**출력:**
```
src/TaskbarAutoHideHook/x64/Release/TaskbarAutoHideHook.dll
```

#### 3단계: DLL을 앱 폴더로 복사

```powershell
Copy-Item `
  "src/TaskbarAutoHideHook/x64/Release/TaskbarAutoHideHook.dll" `
  "src/TaskbarAutoHideSpeed/bin/x64/Release/net9.0-windows/" `
  -Force
```

#### 4단계: 테스트 실행

```powershell
src\TaskbarAutoHideSpeed\bin\x64\Release\net9.0-windows\TaskbarAutoHideSpeed.exe
```

## 빌드 설정

### Debug 빌드

```powershell
.\build.ps1 -Configuration Debug -Platform x64
```

**특징:**
- 디버그 심볼 포함
- 최적화 없음
- 개발/디버깅 용도

### Release 빌드

```powershell
.\build.ps1 -Configuration Release -Platform x64
```

**특징:**
- 최적화됨
- 디버그 심볼 분리 (.pdb)
- 배포 용도

## 프로젝트 구조

```
TaskbarSpeedControl/
├── src/
│   ├── TaskbarAutoHideSpeed/       # C# .NET 9.0 WinForms 앱
│   │   ├── MainForm.cs             # UI
│   │   ├── NativeBridge.cs         # DLL 상호작용
│   │   ├── TaskbarSpeedController.cs # 비즈니스 로직
│   │   ├── SettingsStore.cs        # 설정 영속성
│   │   └── *.csproj                # 프로젝트 파일
│   │
│   └── TaskbarAutoHideHook/        # C++ x64 네이티브 DLL
│       ├── TaskbarAutoHideHook.cpp # 훅 구현
│       └── *.vcxproj               # 프로젝트 파일
│
├── build.ps1                        # 자동 빌드 스크립트
├── README.md                        # 이 파일
└── BUILD.md                         # 빌드 가이드 (이 문서)
```

## 빌드 문제 해결

### "msbuild"를 찾을 수 없음

**해결책 1:** 새 PowerShell 창 열기
```powershell
# 기존 창을 닫고 새 PowerShell 시작
# (환경 변수 재로드)
```

**해결책 2:** Visual Studio Developer PowerShell 사용
```powershell
# Windows 시작 메뉴에서 "Developer PowerShell for VS 2022" 검색 후 실행
```

**해결책 3:** msbuild 전체 경로 사용
```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\msbuild.exe" src\TaskbarAutoHideHook\TaskbarAutoHideHook.vcxproj /p:Configuration=Release /p:Platform=x64
```

### C++ 컴파일 오류

**"Cannot find include"**
- Visual Studio 2022 C++ 워크로드 설치 확인
- `Visual Studio Installer` → 수정 → C++ 데스크톱 개발 선택

### .NET 관련 오류

**"Cannot find .NET 9.0 SDK"**
```powershell
# .NET SDK 설치 확인
dotnet --list-sdks

# 설치 필요:
# https://dotnet.microsoft.com/download
```

### DLL 잠금 오류

**"Cannot copy DLL: file is being used by another process"**

Explorer 프로세스가 DLL을 사용 중입니다. build.ps1에서 자동으로 처리하므로, 수동으로 복사 시:

```powershell
# Explorer 재시작
Stop-Process -Name explorer -Force
Start-Sleep -Milliseconds 500

# DLL 복사
Copy-Item ... -Force

# Explorer 재시작
Start-Process explorer
```

## 배포

### 포터블 배포

1. Release 빌드 완료
2. 다음 파일을 원하는 폴더에 복사:
   ```
   TaskbarAutoHideSpeed.exe
   TaskbarAutoHideHook.dll
   ```
3. 원하는 폴더에서 실행

### 폴더 구조

```
C:\Users\<User>\AppData\Roaming\TaskbarAutoHideSpeed\
├── settings.json       # 설정 (자동 생성)
├── startup.log         # 시작 로그 (자동 생성)
└── hook.log            # 훅 로그 (TASKBAR_AUTOHIDE_TRACE=1 필요)
```

## 디버깅

### 훅 로그 활성화

```powershell
$env:TASKBAR_AUTOHIDE_TRACE=1
.\src\TaskbarAutoHideSpeed\bin\x64\Release\net9.0-windows\TaskbarAutoHideSpeed.exe
```

그 후 로그 확인:
```powershell
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\hook.log" -Tail 50
```

### Visual Studio에서 디버깅

1. Visual Studio 2022에서 `TaskbarSpeedControl.sln` 열기
2. `TaskbarAutoHideSpeed` 프로젝트를 시작 프로젝트로 설정
3. **F5** 또는 **Debug** > **Start Debugging** 클릭
4. 설정 변경 후 Apply 클릭하여 네이티브 DLL 호출 추적

### 프로세스 덤프

앱 크래시 시:
```powershell
# 최근 로그 확인
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\startup.log"
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\hook.log"

# 이벤트 뷰어 확인
eventvwr.msc
# Windows Logs > Application 에서 오류 검색
```

## 성능 측정

Debug vs Release 비교:
```powershell
# Debug 빌드로 측정
.\build.ps1 -Configuration Debug -Platform x64

# 메모리 사용량 확인
Get-Process TaskbarAutoHideSpeed | Select-Object WorkingSet

# Release 빌드로 측정
.\build.ps1 -Configuration Release -Platform x64
Get-Process TaskbarAutoHideSpeed | Select-Object WorkingSet
```

일반적으로 Release 빌드가 Debug 빌드보다 약 30~50% 더 효율적입니다.

## 버전 관리

버전은 다음 파일에서 관리됩니다:
- `src/TaskbarAutoHideSpeed/TaskbarAutoHideSpeed.csproj` — C# 앱 버전
- `src/TaskbarAutoHideHook/TaskbarAutoHideHook.vcxproj` — 네이티브 DLL 버전

버전 업데이트:
```xml
<!-- .csproj 파일에서 -->
<PropertyGroup>
    <AssemblyVersion>1.0.0.0</AssemblyVersion>
    <FileVersion>1.0.0.0</FileVersion>
</PropertyGroup>
```

---

**마지막 업데이트:** May 18, 2026  
**호환성:** Windows 10/11 x64
