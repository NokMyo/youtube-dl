# YouTube MP3 Downloader (C)

군대에서 휴대폰으로 모아 둔 유튜브 링크를 집에서 한꺼번에 MP3로 내려받아 Gear IconX 같은 독립형 플레이어로 옮기기 위한 Windows용 개인 도구입니다.

화면은 일부러 Windows 98/2000 시절의 투박한 Win32 유틸리티 느낌으로 만들었습니다. 프로그램 자체는 **C + Win32 API**로 작성되어 있고, 실제 영상 정보 조회/다운로드는 `yt-dlp.exe`, MP3 변환은 `ffmpeg.exe`를 사용합니다.

> 본인이 소유했거나 다운로드가 허용된 콘텐츠 등 이용 권한이 있는 콘텐츠에 사용하세요.

## 들어간 기능

- 여러 유튜브 링크를 한 번에 붙여넣기
- `.txt` 링크 목록 불러오기
- 중복 URL 자동 제거
- 같은 영상의 다른 형태 URL도 영상 ID 기준으로 다시 중복 제거
- 이미 다운로드한 곡 건너뛰기 옵션
- 실패한 항목만 재시도
- Windows 파일명 금지 문자 자동 제거
- 유튜브 제목을 `아티스트 - 곡명.mp3` 형태로 자동 정리
- 원본 제목 / 정리된 파일명 미리보기
- 다운로드 전 예상 MP3 용량 표시
- 저장 폴더의 현재 MP3 곡 수 / 총 용량 표시
- 다운로드 성공 영상 ID를 `download_history.txt`에 기록
- 순차 다운로드와 개별 진행률 / 전체 진행률 표시

## 이름 자동 정리

가능하면 yt-dlp가 제공하는 `artist` / `track` 정보를 먼저 사용합니다.

그 정보가 없으면 제목을 규칙 기반으로 정리합니다.

예시:

```text
Mrs. GREEN APPLE「点描の唄」Official Music Video
→ Mrs. GREEN APPLE - 点描の唄.mp3

Lauv - I Like Me Better [Official Audio]
→ Lauv - I Like Me Better.mp3
```

`Official Music Video`, `Official Audio`, `Lyric Video` 같은 흔한 표기와 관련 괄호 문구를 제거합니다. 자동 정리가 완벽할 수는 없기 때문에 화면 아래에 저장될 파일명을 미리 보여 줍니다.

## 이미 다운로드한 곡 판별

다운로드가 성공하면 저장 폴더에 다음 파일이 생성됩니다.

```text
download_history.txt
```

여기에 유튜브 영상 ID만 기록합니다. `이미 다운로드한 곡 건너뛰기`가 켜져 있으면:

1. 영상 ID가 이력에 있는지 확인하고
2. 정리된 최종 MP3 파일이 이미 존재하는지도 확인한 뒤
3. 둘 중 하나라도 해당하면 다운로드를 건너뜁니다.

옵션을 끄면 같은 영상을 다시 받을 수 있으며, 기존 파일과 이름이 겹치면 `(2)`, `(3)` 형태로 새 파일을 만듭니다.

## 실행에 필요한 파일

완성된 EXE 옆에 아래 파일을 두는 방식이 가장 간단합니다.

```text
YouTubeMP3Downloader.exe
yt-dlp.exe
ffmpeg.exe
ffprobe.exe   (권장)
```

또는 `yt-dlp.exe`, `ffmpeg.exe`가 Windows `PATH`에 등록되어 있어도 됩니다.

- yt-dlp: https://github.com/yt-dlp/yt-dlp/releases
- FFmpeg: https://ffmpeg.org/download.html

## 빌드

### Visual Studio 2022 + CMake

Developer Command Prompt에서:

```bat
build.bat
```

또는 직접:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

결과물:

```text
build\Release\YouTubeMP3Downloader.exe
```

외부 GUI 라이브러리는 사용하지 않습니다. Windows 기본 Win32 API와 Common Controls만 사용합니다.

## GitHub Actions

저장소에 push하면 Windows 빌드를 자동으로 돌리고 `YouTubeMP3Downloader-windows-x64`라는 Actions artifact로 EXE를 올리도록 설정되어 있습니다.

## 예상 용량 계산

현재 MP3 변환 품질은 320 Kbps로 고정되어 있습니다. 영상 길이를 먼저 조회한 뒤 아래 방식으로 대략적인 최종 MP3 크기를 계산합니다.

```text
영상 길이(초) × 320,000 bit/s ÷ 8
```

실제 파일은 MP3 헤더/메타데이터 등 때문에 약간 달라질 수 있습니다.

## 프로젝트 구조

```text
.
├─ src/
│  └─ main.c
├─ .github/workflows/
│  └─ build.yml
├─ CMakeLists.txt
├─ build.bat
└─ README.md
```
