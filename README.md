# Febius Downrush

Febius Utility Series의 Windows용 미디어 MP3 일괄 다운로드 프로그램입니다. C와 Win32 API로 구현했으며 공식 릴리스는 `yt-dlp`, Deno, FFmpeg, ffprobe, Chromaprint `fpcalc`를 포함한 단일 EXE로 배포합니다.

본인이 소유했거나 다운로드 허가를 받은 콘텐츠에만 사용하세요.

## 지원 환경

- Windows 10/11 x64
- 설치 없이 실행 가능한 `FebiusDownrush.exe`

## 주요 기능

- 여러 링크 및 `.txt` 목록 일괄 처리
- MP3 음질 선택: 128·192·256·320 kbps
- 중복 제거, 기존 파일 건너뛰기, 실패 항목 재시도
- 파일명 자동 정리·직접 편집 및 ID3 메타데이터 삽입
- 예상 용량, 개별 진행률, 전체 진행률 표시
- 다운로드 기록 기반 중복 검사
- Chromaprint `fpcalc` 기반 오디오 지문 중복 판별
- 옵션에서 `동일 녹음 음원 지문 중복 검사`를 독립적으로 켜고 끌 수 있음 (기본값: 켜짐)
- 작업 취소, 시간 제한, 하위 프로세스 종료 및 임시 파일 정리
- CPU 수에 따른 동시 작업량 자동 조절
- SHA-256 검증 기반 자동 업데이트 및 재시작
- 설정 저장, 순환 진단 로그, 단일 인스턴스 실행

## 저장 경로

| 항목 | 경로 |
|---|---|
| 기본 음악 폴더 | `Music\Febius\Downrush` |
| 프로그램 데이터 | `%LOCALAPPDATA%\Febius\Downrush` |
| 내장 도구 | `%LOCALAPPDATA%\Febius\Downrush\tools` |
| 로그 | `%LOCALAPPDATA%\Febius\Downrush\logs\Downrush.log` |
| 임시 작업 | `%TEMP%\Febius\Downrush` |
| 다운로드 기록 | 출력 폴더의 `download_history.txt` |
| 오디오 지문 기록 | 출력 폴더의 `audio_fingerprints.txt` |

첫 실행 시 내장 도구를 프로그램 데이터 폴더에 준비합니다. 이후에는 도구 내용 식별값을 확인해 변경된 경우에만 다시 설치합니다. 기존 Febius YT MP3 Downloader 및 Seowol 설정과 기본 음악 폴더는 필요한 경우 자동 이전합니다.

다운로드가 끝난 MP3는 `fpcalc`로 최대 120초 구간의 Chromaprint 오디오 지문을 생성합니다. 같은 저장 폴더에 동일한 지문을 가진 기존 곡이 있으면 새로 생성된 중복 파일을 제거하고 기존 파일을 다운로드 기록에 연결합니다. `fpcalc`를 사용할 수 없는 환경에서는 기존 URL·다운로드 기록 기반 중복 검사만 사용합니다.

## 사용법

1. 공식 릴리스의 `FebiusDownrush.exe`를 실행합니다.
2. 지원되는 미디어 링크를 입력하거나 `.txt` 목록을 불러옵니다.
3. 음질과 저장 폴더를 확인합니다.
4. `전체 다운로드`를 실행합니다.

업데이트는 새 실행 파일과 릴리스 체크섬이 일치할 때만 적용합니다. 실패하면 기존 실행 파일을 유지합니다.

## 빌드 및 검사

Visual Studio 2022와 CMake 3.20 이상이 필요합니다.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
build\Release\FebiusDownrush.exe --self-test-core
```

단일 EXE 패키지의 내장 도구 검사는 다음과 같습니다.

```bat
FebiusDownrush.exe --self-test-tools
```

Release 빌드는 정적 MSVC 런타임, 전체 프로그램 최적화, 사용하지 않는 코드 제거를 적용합니다.

## 배포 무결성

릴리스에는 다음 파일을 게시합니다.

- `FebiusDownrush.exe`
- `FebiusDownrush.exe.sha256`
- `SIGNING_STATUS.txt`

내장 도구의 버전과 SHA-256은 배포 스크립트에 고정합니다. 코드 서명 인증서가 설정된 경우 Authenticode 서명과 검증을 추가로 수행합니다.

## 라이선스

프로그램 코드는 [LICENSE.txt](LICENSE.txt)를 따릅니다. 내장 구성 요소의 라이선스와 출처는 실행 시 설치되는 `THIRD_PARTY_NOTICES.txt` 및 관련 라이선스 파일에 기록합니다.
