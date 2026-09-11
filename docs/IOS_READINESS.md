# iOS readiness

Preflight date: **2026-09-11**. This records read-only findings. No iOS build or device success is claimed.

**iOS work and testing are deferred at the user's direction.** The current order is: mechanics → aesthetics and assets → full playtest → iOS. The observations below do not block the current mechanics work.

## Current state

- Unreal Engine **5.8.2** is installed on the Mac. At preflight, **before the user applied the iOS component option**, `Engine/Binaries/IOS/UnrealGame.target`, the Shipping receipt, and `Engine/Intermediate/Build/IOS` were missing. `BaseEngine.ini` lines 4014–4016 require the installed iOS arm64 payload. That missing-payload observation has not been refreshed.
- **Xcode 26.6**, **iPhoneOS SDK 26.5**, and Metal tools are available; the checked versions fall within Unreal's allowed range.
- Apple's `xcdevice` sees a USB-connected **iPhone 17 Pro Max**. Discovery does not establish installation or gameplay readiness.
- Rosetta is unavailable. Unreal's bundled x86_64 `idevice_id` and `ideviceinstaller` fail with `Bad CPU type`. This device-tooling problem is separate from C++ compilation.
- Two valid signing identities and 49 unexpired provisioning profiles are present. Exactly one wildcard development profile matches `com.cinderline.game` and the connected device, but its embedded certificate does not match either usable signing identity. Signing is therefore **not ready**.

The user reports enabling the **iOS component** under the Epic Games Launcher's **UE 5.8.2 → Options** and clicking **Apply**. Download and installation completion remain unverified. Earlier automated Launcher interaction returned `noWindowsAvailable`.

## Deferred iOS steps

Resume these steps after the mechanics, aesthetics/assets, and full-playtest passes, when iOS work resumes.

1. Verify the applied iOS component has finished installing and the required platform payload and receipts exist.
2. Run this first compile-only check:

   ```sh
   '/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh' Cinderline IOS Development '/Users/chirag13/Documents/ChatGPT/starCraft/Cinderline.uproject' -WaitMutex -NoLink -SkipDeploy
   ```

   Passing this command proves compilation only. It intentionally skips linking and deployment.

3. Resolve signing with a development certificate and private key that match the device-authorized provisioning profile, or generate a matching profile through the appropriate Apple development account. Keep identity and profile details private.
4. Resolve the x86_64 device-tool dependency before using Unreal's bundled deployment utilities, through Rosetta or supported native tooling.
5. Link and cook/package the project, then verify signing before installation. From the repository root, `./scripts/unreal.sh package-ios` performs build, cook, stage, pak, package, and archive. This command has **not been run**.
6. Install and launch on the connected iPhone, then test physical touch input and a complete skirmish on the device.

## Evidence still required

| Stage | Status |
| --- | --- |
| iOS C++ compilation | Not demonstrated; payload installation completion unverified |
| iOS linking | Not demonstrated |
| Cook, package, and archive | Not demonstrated |
| Valid application signing | Blocked by profile/certificate mismatch |
| Device installation and launch | Not demonstrated |
| Physical touch controls and gameplay | Not demonstrated |

Keep evidence for these stages separate. A compile result, packaged archive, or detected phone cannot establish the later stages.
