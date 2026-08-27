# Febius Downrush

**Febius Utility Series · FAST · DIRECT · EFFICIENT**

휴대폰에서 모아 둔 YouTube 링크를 Windows PC에서 한꺼번에 MP3로 정리해 내려받는 C + Win32 유틸리티입니다.

Downrush는 속도와 효율을 우선하는 Febius 제품군의 다운로드 유틸리티입니다. 화면은 Windows 95/98 시절의 투박한 느낌을 유지하며, 별도 웹 기반 UI 없이 Windows 기본 컨트롤로 동작합니다. 실제 다운로드/변환에는 `yt-dlp`, `yt-dlp-ejs`, `Deno`, `ffmpeg`, `ffprobe`를 사용하고 공식 릴리스의 `FebiusDownrush.exe`에 필요한 도구를 모두 포함합니다.

> 본인이 소유했거나 다운로드가 허용된 콘텐츠 등 이용 권한이 있는 콘텐츠에 사용하세요.

지원 환경은 Windows 10/11 x64입니다. 시스템 DPI에 맞춰 창·컨트롤·아이콘 크기를 조정하며 키보드 `Tab` 이동을 지원합니다. 논리 프로세서 수에 따라 정보 조회와 다운로드 동시 작업 수를 자동 조절해 저사양 PC에서도 화면 반응성을 유지합니다.

## 주요 기능

- 여러 YouTube 링크 한 번에 추가
- `.txt` 링크 목록 불러오기
- 옵션 메뉴에서 MP3 음질 선택 (`128`, `192`, `256`, `320 kbps`)
- 중복 URL 및 동일 영상 ID 자동 제거
- 이미 다운로드한 곡 건너뛰기 옵션
- 실패한 항목만 다시 다운로드
- Windows 파일명 금지 문자 자동 제거
- `아티스트 - 곡명.mp3` 형태의 파일명 자동 정리와 공식 영상 표기 괄호 제거
- 원본 제목 / 정리된 파일명 미리보기
- 최종 파일명을 직접 편집하고 항목별로 고정
- MP3 ID3 메타데이터 삽입
- 다운로드 전 예상 MP3 용량 표시
- 저장 폴더의 MP3 곡 수 / 총 용량 표시
- 영상 ID·음질·실제 파일명을 기록하는 `download_history.txt` v2 이력 관리
- CPU 성능에 따라 최대 메타데이터 4개 병렬 조회와 2개 병렬 다운로드
- 항목 상태에 개별 진행률 표시 및 전체 진행률 표시
- 다운로드 취소, 시간 제한, 하위 프로세스 일괄 종료
- 자주 쓰는 버튼 3개만 남기고 부가 작업과 설정을 모은 클래식 메뉴
- 음질·파일명·중복·저장 폴더·업데이트 알림 설정 자동 저장
- Windows 95/98풍 클래식 메뉴·상태 표시줄
- 제품 버전·저작권·라이선스·외부 구성 요소를 확인할 수 있는 제품 정보 창
- 앱 안에서 최신 버전을 확인하고 새 버전만 알려 주는 시작 알림
- 별도 시작 화면에서 구성 요소 준비 상태를 표시한 뒤 프로그램 창 실행
- 단일 인스턴스 실행, 고유 임시 폴더와 비정상 종료 잔여 파일 자동 정리
- `%LOCALAPPDATA%\Febius\Downrush\logs\Downrush.log` 순환 로그

## 기본 저장 위치

```text
Music\Febius\Downrush
```

## 단일 EXE 배포

공식 릴리스에서는 아래 파일 하나만 받으면 됩니다.

```text
FebiusDownrush.exe
```

첫 실행 시 별도 시작 화면이 표시되며 내장된 `yt-dlp`, `Deno`, `ffmpeg`, `ffprobe`가 `%LOCALAPPDATA%\Febius\Downrush\tools`에 자동으로 준비됩니다. 이후 실행에서는 설치 스탬프를 확인한 뒤 준비된 도구를 재사용합니다.

기존 `%LOCALAPPDATA%\FebiusYTMP3Downloader` 또는 Seowol 설정·도구는 새 제품 폴더로 자동 이전합니다. 기본 음악 폴더로 사용하던 `Music\YouTubeMP3`도 새 경로가 아직 없으면 `Music\Febius\Downrush`로 옮깁니다. 사용자가 직접 지정한 다른 저장 폴더는 그대로 유지합니다.

도구 실행 경로는 내장 폴더 또는 프로그램 폴더로 제한합니다. 현재 작업 디렉터리나 `PATH`에서 이름만 같은 실행 파일을 대신 실행하지 않습니다.

## 빌드

Visual Studio 2022 + CMake 기준:

```bat
build.bat
```

또는:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

결과물:

```text
build\Release\FebiusDownrush.exe
```

외부 GUI 라이브러리는 사용하지 않고 Windows 기본 Win32 API와 Common Controls를 사용합니다.
Release 빌드에는 전체 프로그램 최적화와 사용하지 않는 코드 제거가 적용됩니다.

빌드된 실행 파일의 파일명·명령행·버전 처리 자체 검사는 아래처럼 실행할 수 있습니다.

```bat
build\Release\FebiusDownrush.exe --self-test-core
```

단일 파일 패키지에 포함된 네 실행 파일이 실제로 시작되고 버전을 반환하는지는 다음 명령으로 확인합니다.

```bat
FebiusDownrush.exe --self-test-tools
```

프로그램은 실행 결과가 사용자의 전역 `yt-dlp` 설정에 따라 달라지지 않도록 외부 설정 파일을 읽지 않습니다.

## 이름 자동 정리

가능하면 `yt-dlp`가 제공하는 `artist` / `track` 정보를 우선 사용합니다. 정보가 없으면 제목을 규칙 기반으로 정리합니다.

```text
Mrs. GREEN APPLE「点描の唄」Official Music Video
→ Mrs. GREEN APPLE - 点描の唄.mp3

Lauv - I Like Me Better [Official Audio]
→ Lauv - I Like Me Better.mp3

가수 - 노래 (공식 뮤비)
→ 가수 - 노래.mp3

가수 - 노래 (Live)
→ 가수 - 노래 (Live).mp3
```

자동 정리가 완벽할 수는 없으므로 프로그램에서 최종 파일명을 직접 고칠 수 있습니다. `파일명 적용`을 누른 항목은 이후 이름 정리 옵션을 바꿔도 사용자 지정 이름을 유지합니다.

## 다운로드 기록과 임시 파일

`download_history.txt`는 출력 폴더마다 생성됩니다. v2 형식은 영상 ID뿐 아니라 선택 음질과 실제 저장 파일명을 함께 기록합니다. 기록된 파일이 삭제되었거나 다른 음질을 선택한 경우에는 다시 다운로드합니다. 구버전의 ID 전용 기록은 기본값이었던 320 kbps 기록으로 호환해서 읽습니다.

다운로드는 `%TEMP%\Febius\Downrush` 아래의 실행별 고유 디렉터리에서 진행한 뒤 완성된 MP3만 목적 폴더로 옮깁니다. 취소 또는 실패 시 해당 임시 폴더를 정리하고, 24시간 이상 남은 비정상 종료 잔여 폴더는 다음 실행 때 제거합니다.

## 배포 무결성

배포 워크플로는 `yt-dlp`, Deno, FFmpeg의 버전과 SHA-256을 고정합니다. 릴리스에는 실행 파일과 함께 `FebiusDownrush.exe.sha256`, `SIGNING_STATUS.txt`가 게시됩니다. 저장소에 Windows 코드 서명 인증서 비밀 값이 설정된 경우에는 최종 단일 EXE를 Authenticode로 서명한 뒤 다시 검증합니다. 인증서가 없으면 서명되지 않았다는 상태 파일을 명시적으로 게시합니다.

## 브랜드 이미지 자리

Downrush 제품 아이콘은 `src/app.ico`, 프로그램 정보의 Febius 공통 심볼은 `src/febius-symbol.ico` 자리를 사용합니다. 원본 SVG·PNG와 권장 ICO 크기는 `assets/branding/README.md`에 정리되어 있습니다. 심볼이 아직 없으면 정보 화면은 가벼운 기본 `F` 표식을 사용합니다.

고정 버전의 최신 여부는 주간 `Dependency freshness` 워크플로가 확인하고, 실제 YouTube 추출 호환성은 별도의 주간 스모크 테스트가 점검합니다.

## 프로젝트 구조

```text
.
├─ src/
│  ├─ app.rc / resource.h / version.h.in
│  ├─ command_line.c / command_line.h
│  ├─ filename.c / filename.h
│  ├─ history.c / history.h
│  ├─ logger.c / logger.h
│  ├─ process_runner.c / process_runner.h
│  └─ main.c / app.manifest / app.ico
├─ assets/app-icon.svg
├─ assets/branding/README.md
├─ .github/scripts/
├─ .github/workflows/
│  ├─ build.yml
│  ├─ release-v1.yml
│  ├─ dependency-check.yml
│  └─ youtube-smoke.yml
├─ CMakeLists.txt
├─ build.bat
└─ README.md
```

## 라이선스

프로그램 자체 코드는 [LICENSE.txt](LICENSE.txt)의 조건이 적용됩니다. `yt-dlp`와 `yt-dlp-ejs`는 Unlicense, Deno는 MIT License, 현재 FFmpeg Essentials Build는 GPLv3 조건이 적용됩니다. 원문 라이선스, 고정 버전, 소스 주소는 실행 시 준비되는 `THIRD_PARTY_NOTICES.txt`와 관련 파일에서 확인할 수 있습니다.
