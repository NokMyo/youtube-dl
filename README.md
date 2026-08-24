# Seowol YT MP3 Downloader

휴대폰에서 모아 둔 YouTube 링크를 Windows PC에서 한꺼번에 MP3로 정리해 내려받는 C + Win32 유틸리티입니다.

화면은 Windows 98/2000 시절의 투박한 유틸리티 느낌을 유지하고, 실제 다운로드/변환 엔진은 `yt-dlp`, `ffmpeg`, `ffprobe`를 사용합니다. 공식 릴리스의 `SeowolYTMP3Downloader.exe`에는 이 도구들이 함께 포함되어 있어 별도 설치 없이 실행할 수 있습니다.

> 본인이 소유했거나 다운로드가 허용된 콘텐츠 등 이용 권한이 있는 콘텐츠에 사용하세요.

## 주요 기능

- 여러 YouTube 링크 한 번에 추가
- `.txt` 링크 목록 불러오기
- 중복 URL 및 동일 영상 ID 자동 제거
- 이미 다운로드한 곡 건너뛰기 옵션
- 실패한 항목만 다시 다운로드
- Windows 파일명 금지 문자 자동 제거
- `아티스트 - 곡명.mp3` 형태의 파일명 자동 정리
- 원본 제목 / 정리된 파일명 미리보기
- 다운로드 전 예상 MP3 용량 표시
- 저장 폴더의 MP3 곡 수 / 총 용량 표시
- `download_history.txt` 기반 다운로드 이력 관리
- 순차 다운로드 및 개별/전체 진행률 표시

## 기본 저장 위치

```text
Music\YouTubeMP3
```

## 단일 EXE 배포

공식 릴리스에서는 아래 파일 하나만 받으면 됩니다.

```text
SeowolYTMP3Downloader.exe
```

첫 실행 시 내장된 `yt-dlp`, `ffmpeg`, `ffprobe`가 `%LOCALAPPDATA%\SeowolYTMP3Downloader\tools`에 자동으로 준비되고 이후 재사용됩니다.

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
build\Release\SeowolYTMP3Downloader.exe
```

외부 GUI 라이브러리는 사용하지 않고 Windows 기본 Win32 API와 Common Controls를 사용합니다.

빌드된 실행 파일의 파일명·명령행 처리 자체 검사는 아래처럼 실행할 수 있습니다.

```bat
build\Release\SeowolYTMP3Downloader.exe --self-test-core
```

프로그램은 실행 결과가 사용자의 전역 `yt-dlp` 설정에 따라 달라지지 않도록 외부 설정 파일을 읽지 않습니다.

## 이름 자동 정리

가능하면 `yt-dlp`가 제공하는 `artist` / `track` 정보를 우선 사용합니다. 정보가 없으면 제목을 규칙 기반으로 정리합니다.

```text
Mrs. GREEN APPLE「点描の唄」Official Music Video
→ Mrs. GREEN APPLE - 点描の唄.mp3

Lauv - I Like Me Better [Official Audio]
→ Lauv - I Like Me Better.mp3
```

자동 정리가 완벽할 수는 없으므로 프로그램에서 최종 파일명을 미리 확인할 수 있습니다.

## 프로젝트 구조

```text
.
├─ src/
│  └─ main.c
├─ .github/workflows/
│  ├─ build.yml
│  └─ release-v1.yml
├─ CMakeLists.txt
├─ build.bat
└─ README.md
```
