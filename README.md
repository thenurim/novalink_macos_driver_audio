# NovaLINK MacOS Passthrough Audio Driver

- Forked from [BackgroundMusic](https://github.com/kyleneideck/BackgroundMusic)
- 가상 오디오 장치(HAL AudioServerPlugIn) + 패스스루 앱 + XPCHelper.
- 기존 BackgroundMusic에서 앱별 볼륨 조절을 제거.
- 오디오 드라이버를 통해 데스크탑의 사운드를 캡쳐할 수 있습니다.

## 구성

| 구성요소 | 경로 | 역할 |
|---------|------|------|
| Driver | `NovaLinkAudioDriver/` | 시스템 기본 출력으로 쓰이는 가상 장치. 오디오를 링버퍼로 루프백 |
| App | `NovaLINKApp/` → `NovaLINK Audio Passthrough.app` | 메뉴바에서 **Output Device** 선택, 가상 장치 → 실제 장치 playthrough |
| XPCHelper | `NovaLINKApp/NovaLINKXPCHelper/` | Driver ↔ App IO 시작 동기화, 비정상 종료 시 기본 장치 복구 |

드라이버만 활성화되면 스피커로 소리가 나지 않습니다. NovaLINK 패스스루 앱이 실행 중이어야 로컬 재생이 됩니다.

## 빌드

```bash
# Driver
xcodebuild -project NovaLinkAudioDriver/NovaLINKDriver.xcodeproj \
  -scheme "NovaLINK Audio Device" -configuration Release \
  -derivedDataPath build/DerivedData-Driver \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_ALLOWED=NO build

# Passthrough app + XPCHelper
xcodebuild -project NovaLINKApp/NovaLINKApp.xcodeproj \
  -scheme "NovaLINK" -configuration Release \
  -derivedDataPath build/DerivedData-App \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_ALLOWED=NO build

xcodebuild -project NovaLINKApp/NovaLINKApp.xcodeproj \
  -scheme NovaLINKXPCHelper -configuration Release \
  -derivedDataPath build/DerivedData-App \
  CODE_SIGN_IDENTITY="-" CODE_SIGNING_ALLOWED=NO build
```

산출물:

- `build/DerivedData-Driver/Build/Products/Release/NovaLINK Audio Device.driver`
- `build/DerivedData-App/Build/Products/Release/NovaLINK Audio Passthrough.app`
- `build/DerivedData-App/Build/Products/Release/NovaLINKXPCHelper.xpc`

## Bundle IDs

- Driver: `life.thenurim.novalink.AudioDriver`
- Passthrough App: `life.thenurim.novalink.App`
- XPCHelper: `life.thenurim.novalink.XPCHelper`
- Device UID: `NovaLINKDevice`

## LICENSE
Copyright © 2022-2026 [THENURIM](https://www.thenurim.life).
Licensed under [GPLv2](https://www.gnu.org/licenses/gpl-2.0.html), or any later version.

**NovaLINK MacOS Passthrough Audio Driver** includes code from:

- [Background Music
](https://github.com/kyleneideck/BackgroundMusic)
- [Core Audio User-Space Driver
  Examples](https://developer.apple.com/library/mac/samplecode/AudioDriverExamples/Introduction/Intro.html), [original
  license](LICENSE-Apple-Sample-Code), Copyright (C) 2013 Apple Inc. All Rights Reserved.
- [Core Audio Utility
  Classes](https://developer.apple.com/library/content/samplecode/CoreAudioUtilityClasses/Introduction/Intro.html),
  [original license](LICENSE-Apple-Sample-Code), Copyright (C) 2014 Apple Inc. All Rights Reserved.