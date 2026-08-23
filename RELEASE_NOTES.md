# YouTube MP3 Downloader v1.0

첫 정식 릴리즈입니다.

## 주요 기능

- 여러 YouTube URL을 한 번에 추가
- `.txt` 링크 목록 불러오기
- 중복 URL 및 동일 영상 ID 자동 제거
- 이미 다운로드한 곡 건너뛰기 옵션
- 실패한 항목만 다시 다운로드
- Windows 파일명 금지 문자 자동 제거
- `아티스트 - 곡명.mp3` 형태의 파일명 자동 정리
- 원본 제목 / 정리된 파일명 미리보기
- 다운로드 전 예상 MP3 용량 표시
- 현재 저장 폴더의 MP3 곡 수 / 총 용량 표시
- `download_history.txt` 기반 다운로드 이력 관리
- 1999년대 Windows 98/2000 스타일의 C + Win32 UI

## 실행 환경

Windows x64용입니다.

`YouTubeMP3Downloader.exe`와 함께 `yt-dlp.exe`, `ffmpeg.exe`가 필요합니다. 같은 폴더에 두거나 Windows PATH에 등록하면 됩니다. `ffprobe.exe`도 함께 두는 것을 권장합니다.

```text
YouTubeMP3Downloader.exe
yt-dlp.exe
ffmpeg.exe
ffprobe.exe
```

실제 다운로드 및 사이트 대응은 외부 `yt-dlp`에 의존하므로, YouTube 쪽 변경으로 문제가 생기면 최신 yt-dlp로 교체해 주세요.

> 본인이 소유했거나 다운로드가 허용된 콘텐츠 등 이용 권한이 있는 콘텐츠에 사용하세요.
