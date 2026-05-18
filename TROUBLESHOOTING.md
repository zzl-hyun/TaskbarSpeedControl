# Troubleshooting Guide

Taskbar Auto-Hide Speed Controller 사용 중 문제가 발생하면 이 가이드를 참조하세요.

## 일반적인 문제

### 1. 애니메이션 속도가 변하지 않음

**증상:** 설정을 적용해도 작업표시줄 애니메이션이 빨라지지 않음

**진단:**
```powershell
# 설정 파일 확인
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\settings.json"

# 로그 확인
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\startup.log"
```

**해결책:**

1. **Apply 클릭 재확인**
   - 설정 변경 후 반드시 **Apply** 버튼 클릭
   - 상태 표시줄에 "Successfully applied" 메시지 확인

2. **작업표시줄 자동숨김 활성화 확인**
   - Windows 설정 > Taskbar > Automatically hide the taskbar in desktop mode 활성화
   - 마우스를 화면 아래 모서리로 이동하여 작업표시줄 자동 숨김 확인

3. **Explorer 프로세스 재시작**
   ```powershell
   Stop-Process -Name explorer -Force
   Start-Sleep -Seconds 2
   Start-Process explorer
   ```

4. **앱 재시작**
   ```powershell
   Stop-Process -Name TaskbarAutoHideSpeed -ErrorAction SilentlyContinue
   # 앱 다시 실행
   ```

### 2. "Native hook DLL load failed" 오류

**증상:** 앱 실행 시 네이티브 DLL 로드 실패 메시지

**진단:**
```powershell
# DLL 파일 존재 확인
Get-ChildItem "$PSScriptRoot\TaskbarAutoHideHook.dll"
Get-ChildItem "$env:APPDATA\...\TaskbarAutoHideHook.dll"

# 로그 확인
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\startup.log"
```

**해결책:**

1. **DLL 위치 확인**
   - DLL이 exe와 같은 폴더에 있는지 확인
   - 예: `C:\Users\<User>\...\TaskbarAutoHideSpeed.exe` 와 `TaskbarAutoHideHook.dll` 이 같은 폴더

2. **관리자 권한 실행**
   - 앱을 마우스 우클릭 > "관리자 권한으로 실행"

3. **DLL 재배포**
   - 빌드 재수행: `.\build.ps1 -Configuration Release`
   - 혹은 수동 복사:
     ```powershell
     Copy-Item "src\TaskbarAutoHideHook\x64\Release\TaskbarAutoHideHook.dll" `
       "src\TaskbarAutoHideSpeed\bin\x64\Release\net9.0-windows\" -Force
     ```

4. **안티바이러스 확인**
   - 안티바이러스/Windows Defender가 DLL 로드 차단하는지 확인
   - 잠시 비활성화하고 다시 시도

### 3. "Initialize failed" 메시지

**증상:** Apply 클릭 후 "Initialize failed" 오류

**원인:** 네이티브 DLL 초기화 실패 (Explorer 주입 실패 등)

**해결책:**

1. **Explorer 실행 확인**
   ```powershell
   Get-Process explorer
   ```
   실행 중이 아니면 수동 시작:
   ```powershell
   Start-Process explorer
   ```

2. **설정 파일 확인**
   ```powershell
   # 설정 파일 손상 검사
   Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\settings.json"
   # 유효한 JSON인지 확인
   ```

3. **로그 상세 조회**
   ```powershell
   $env:TASKBAR_AUTOHIDE_TRACE=1
   # 앱 재시작
   Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\hook.log" -Tail 50
   ```

4. **시스템 무결성 검사**
   ```powershell
   # 관리자 PowerShell에서
   sfc /scannow
   ```

### 4. 앱이 시작되지 않음

**증상:** exe 클릭해도 아무 반응 없음 또는 즉시 종료

**진단:**
```powershell
# 프로세스 실행 확인
Get-Process TaskbarAutoHideSpeed

# 오류 로그 확인
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\startup.log"

# 최근 시스템 이벤트 확인
Get-EventLog -LogName Application -Newest 20
```

**해결책:**

1. **.NET Runtime 설치 확인**
   ```powershell
   dotnet --list-runtimes
   # .NET 9.0 이상 확인
   ```
   없으면 설치: https://dotnet.microsoft.com/download

2. **관리자 권한으로 실행**
   ```powershell
   Start-Process -FilePath "TaskbarAutoHideSpeed.exe" -Verb RunAs
   ```

3. **명령줄에서 실행 및 오류 확인**
   ```powershell
   cd <앱 폴더>
   .\TaskbarAutoHideSpeed.exe
   # 오류 메시지 확인
   ```

4. **앱 재설치**
   - 앱 폴더 전체 삭제
   - 빌드 재수행: `.\build.ps1 -Configuration Release`

### 5. "Explorer injection failed" 오류

**증상:** 로그에 "InjectSelfIntoExplorer: failed" 메시지

**진단:**
```powershell
# Explorer 프로세스 ID 확인
Get-Process explorer | Select-Object ProcessName, Id

# 현재 사용자 권한 확인
[System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
```

**해결책:**

1. **관리자 권한 확인**
   - 관리자 권한이 필요할 수 있습니다
   - 우클릭 > "관리자 권한으로 실행"

2. **Explorer 재시작**
   ```powershell
   Stop-Process -Name explorer -Force
   Start-Sleep -Seconds 2
   Start-Process explorer
   ```

3. **SELinux/AppLocker 정책 확인**
   - 엔터프라이즈 환경에서 DLL 로드 차단 가능
   - 시스템 관리자 문의

## 고급 문제

### 6. 특정 상황에서만 작동하지 않음

**예: 여러 모니터에서 작동하지 않음**

**진단:**
```powershell
$env:TASKBAR_AUTOHIDE_TRACE=1
# 앱 재실행 및 여러 모니터에서 테스트
Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\hook.log"
```

**해결책:**
- 모든 모니터에서 자동숨김 설정 확인
- 주 모니터에서만 테스트 시작

### 7. 높은 CPU 사용률

**증상:** 앱 실행 후 CPU 사용률 증가

**원인:** 훅 워커 스레드 과도한 로깅 또는 높은 재스캔 빈도

**진단:**
```powershell
Get-Process TaskbarAutoHideSpeed | Select-Object Name, ProcessorAffinity, Threads
```

**해결책:**

1. **로깅 비활성화**
   ```powershell
   # 환경 변수 제거
   $env:TASKBAR_AUTOHIDE_TRACE=""
   # 앱 재시작
   ```

2. **프레임 레이트 낮추기**
   - UI에서 Frame Rate를 60 또는 30으로 설정
   - Apply 클릭

3. **작업표시줄 설정 다시 확인**
   - 자동숨김이 실제로 필요한지 확인

### 8. Windows 업데이트 후 작동하지 않음

**증상:** Windows 업데이트 후 갑자기 작동 중단

**원인:** Explorer.exe 구조 변경 또는 Taskbar.View.dll 업데이트

**해결책:**

1. **앱 재시작**
   ```powershell
   Stop-Process -Name TaskbarAutoHideSpeed -ErrorAction SilentlyContinue
   Start-Process TaskbarAutoHideSpeed.exe
   ```

2. **Explorer 재시작**
   ```powershell
   Stop-Process -Name explorer -Force
   Start-Sleep -Seconds 2
   Start-Process explorer
   ```

3. **설정 초기화**
   - "Restore Defaults" 클릭
   - "Apply" 클릭
   - 다시 설정 변경 후 Apply

4. **프로젝트 재빌드**
   - 최신 코드로 재빌드: `.\build.ps1 -Configuration Release`

## 로그 해석

### startup.log 예시

```
[2026-05-18 13:17:28.873] Main entered
[2026-05-18 13:17:28.917] Starting message loop; startHidden=False
```

정상: 앱 시작 및 메시지 루프 진입

### hook.log 예시 (TASKBAR_AUTOHIDE_TRACE=1 필수)

```
DllMain PROCESS_ATTACH
HookWorkerThread started
Loaded settings: show=500 hide=500 fps=90
TaskbarAutoHideHook_Initialize called
InjectSelfIntoExplorer: success
InstallProcessHooks: patch success
Taskbar slide detected: show speedup=500 oldVisible=5120 newVisible=23040
```

정상:
- PROCESS_ATTACH: DLL 로드됨
- Initialize called: 초기화 시작
- Inject success: Explorer에 주입됨
- Patch success: IAT 패치 성공
- Taskbar slide detected: 슬라이드 감지

## 수집해야 할 디버그 정보

버그 리포트 시 다음 정보 수집:

1. **로그 파일**
   ```powershell
   Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\startup.log"
   Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\hook.log"
   ```

2. **시스템 정보**
   ```powershell
   [System.Environment]::OSVersion
   Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model
   ```

3. **Windows 버전**
   ```powershell
   Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" |
     Select-Object CurrentVersion, DisplayVersion, BuildLabEx
   ```

4. **Explorer 버전**
   ```powershell
   (Get-Command explorer.exe).FileVersionInfo
   ```

5. **앱 설정**
   ```powershell
   Get-Content "$env:APPDATA\TaskbarAutoHideSpeed\settings.json"
   ```

## 추가 리소스

- **공식 저장소:** [GitHub Repository URL]
- **.NET 트러블슈팅:** https://docs.microsoft.com/en-us/dotnet/
- **Windows 트러블슈팅:** https://support.microsoft.com/windows

---

**마지막 업데이트:** May 18, 2026
