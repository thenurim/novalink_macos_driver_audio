# NovaLINK macOS Audio

가상 오디오 장치(HAL AudioServerPlugIn) + 메뉴바 패스스루 앱 + XPCHelper.

## 구성

| 구성요소 | 경로 | 역할 |
|---------|------|------|
| Driver | `NovaLinkAudioDriver/` | 시스템 기본 출력으로 쓰이는 가상 장치. 오디오를 링버퍼로 루프백 |
| App | `BGMApp/` → `NovaLINK.app` | 메뉴바에서 **Output Device** 선택, 가상 장치 → 실제 장치 playthrough |
| XPCHelper | `BGMApp/BGMXPCHelper/` | Driver ↔ App IO 시작 동기화, 비정상 종료 시 기본 장치 복구 |

드라이버만 활성화되면 스피커로 소리가 나지 않습니다. NovaLINK 패스스루 앱이 실행 중이어야 로컬 재생이 됩니다.

## 빌드

```bash
# Driver
xcodebuild -project NovaLinkAudioDriver/NovaLINKDriver.xcodeproj \
  -scheme "NovaLINK Audio Device" -configuration Release \
  -derivedDataPath build/DerivedData-Driver \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_ALLOWED=NO build

# Passthrough app + XPCHelper
xcodebuild -project BGMApp/BGMApp.xcodeproj \
  -scheme "Background Music" -configuration Release \
  -derivedDataPath build/DerivedData-App \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_ALLOWED=NO build

xcodebuild -project BGMApp/BGMApp.xcodeproj \
  -scheme BGMXPCHelper -configuration Release \
  -derivedDataPath build/DerivedData-App \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_ALLOWED=NO build
```

산출물:

- `build/DerivedData-Driver/Build/Products/Release/NovaLINK Audio Device.driver`
- `build/DerivedData-App/Build/Products/Release/NovaLINK.app`
- `build/DerivedData-App/Build/Products/Release/NovaLINKXPCHelper.xpc`

## officeone 연동

Electron이 시작되면:

1. HAL 드라이버 설치 (`/Library/Audio/Plug-Ins/HAL/`)
2. XPCHelper 설치 (`/Library/Application Support/NovaLINK/` + LaunchDaemon)
3. `NovaLINK Audio Passthrough.app` 실행 (메뉴바 Output Device 선택)

별도 앱을 `/Applications`에 설치할 필요 없이, NovaLINK Electron 패키지 리소스에서 companion을 실행합니다.

## Bundle IDs

- Driver: `life.thenurim.novalink.AudioDriver`
- Passthrough App: `life.thenurim.novalink.App`
- XPCHelper: `life.thenurim.novalink.XPCHelper`
- Device UID: `NovaLINKDevice`
