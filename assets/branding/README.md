# Febius branding slots

브랜드 원본 이미지는 이 폴더에 보관합니다.

- `febius-symbol.svg` 또는 `febius-symbol.png`: Febius 전체 제품군 공통 심볼 원본
- `downrush-icon.svg` 또는 `downrush-icon.png`: Downrush 제품 아이콘 원본
- `src/febius-symbol.ico`: 프로그램 정보 배너에 넣을 Windows 아이콘 자원
- `src/app.ico`: 실행 파일·작업 표시줄·창에 표시할 Downrush 아이콘 자원

`src/febius-symbol.ico`를 추가한 뒤 `src/app.rc`의 `IDI_FEBIUS_SYMBOL` 줄을 활성화하면 정보 화면이 기존 `F` 대체 표시 대신 심볼을 자동으로 사용합니다. `src/app.ico`는 같은 파일명으로 교체하면 별도 코드 수정 없이 반영됩니다.

권장 크기는 SVG/PNG 원본 1024×1024 이상, ICO는 16·24·32·48·64·128·256px 다중 크기입니다.
