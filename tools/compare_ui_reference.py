"""Export an offline comparison of unmodified reference/before/after captures.

Geometry differences are measured per region. No whole-image similarity or
completion percentage is invented. The example HTML and synthetic PDFs contain
different documents, so their page contents are not a visual-regression oracle.
"""
import argparse
import base64
import html
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('reference', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('before', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source', required=True)
    parser.add_argument('--run', required=True)
    parser.add_argument('--before-label', default='3bb3efd')
    args = parser.parse_args()
    summary = json.loads((args.native / 'integrated/summary.json').read_text(encoding='utf-8-sig'))
    fragments = ['''<!doctype html><html lang="ko"><meta charset="utf-8"><title>LUNA v0.9.1 native UI comparison</title>
<style>body{font:14px/1.6 system-ui;color:#263247;background:#f3f6fc;margin:24px}h1{font-size:24px}h2{font-size:20px}h3{font-size:15px}.captures{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}figure{margin:0;background:white;border:1px solid #e0e7f1;border-radius:10px;padding:10px;min-width:0}img{width:100%;height:auto;display:block}table{border-collapse:collapse;width:100%;background:white;margin:18px 0}th,td{border:1px solid #e0e7f1;padding:8px;text-align:left}code{word-break:break-all}.note{background:#fff5df;padding:12px;border-left:4px solid #b7791f}@media(max-width:1000px){.captures{grid-template-columns:1fr}}.actual .captures{grid-template-columns:repeat(3,max-content)}.actual img{width:auto}</style>
<h1>승인 HTML v0.9.1 → Windows 네이티브 UI</h1>
<button onclick="document.body.classList.toggle('actual')">원본 픽셀 크기 / 화면에 맞추기</button>
<p>아래 PNG는 원본 그대로 내장했다. HTML은 Windows Edge의 맑은 고딕, Qt는 Windows C++ 앱의 실제 위젯 렌더다. 동일 해상도끼리 비교하며 예시 논문과 합성 PDF의 본문은 비교 대상에서 제외한다.</p>''']
    fragments.append('<p>개발 소스 <code>' + html.escape(args.source) + '</code> · Windows 실행 <code>' + html.escape(args.run) + '</code> · 네이티브 검사 accepted=' + str(summary['accepted']).lower() + '</p>')
    fragments.append('<p class="note">Ctrl+W는 기준 빌드에서 사용자 Windows 10의 30회 반복 통과가 확인됐다. 이번 빌드의 실제 PC 화면·간헐 지연 및 물리 모니터 OS 배율 전환은 별도 확인이 필요하다. 크기별 위젯 캡처와 실제 데스크톱 캡처를 아래에서 구분한다.</p>')
    measurements = {}
    for width in (1366, 1920):
        reference = json.loads((args.reference / f'html-windows-{width}-metrics.json').read_text(encoding='utf-8-sig'))
        directory = args.native / 'integrated/scale-1'
        geometry = json.loads((directory / f'edit.json.five-tabs.{width}.geometry.json').read_text(encoding='utf-8-sig'))
        fragments.append(f'<h2>{width} × {768 if width == 1366 else 1080}</h2><div class="captures">')
        for label, path in (
            ('승인 HTML · Windows Edge', args.reference / f'html-windows-{width}.png'),
            ('변경 전 · ' + args.before_label, args.before / f'integrated/scale-1/edit.json.five-tabs.{width}.png'),
            ('변경 후 · ' + args.source[:7], directory / f'edit.json.five-tabs.{width}.png')):
            image = base64.b64encode(path.read_bytes()).decode()
            fragments.append('<figure><h3>' + html.escape(label) + '</h3><img alt="' + html.escape(label) + '" src="data:image/png;base64,' + image + '"></figure>')
        fragments.append('</div><table><tr><th>영역</th><th>HTML x/y/폭/높이</th><th>Qt x/y/폭/높이</th><th>Qt − HTML</th></tr>')
        rows = []
        for label, source, target in (
            ('브랜드/메뉴 행', '.titlebar', 'LunaMainMenuBar'),
            ('공통 툴바', '.toolbar', 'LunaCommonReaderToolbar'),
            ('폴더 패널', '.navpanel', 'LunaNavigation'),
            ('논문 목록 패널', '.listpanel', 'LunaDocuments'),
            ('논문/페이지 전환', '.modebar', 'LunaContentModes'),
            ('PDF 리더', '.reader', 'LunaPdfTabs'),
            ('PDF 탭 행', '.tabstrip', 'LunaPdfTabBar')):
            expected = reference['metrics'][source]
            actual = geometry[target]
            keys = ('x', 'y', 'width', 'height')
            delta = {key: round(actual[key] - expected[key], 3) for key in keys}
            rows.append({'region': label, 'reference': expected, 'native': actual, 'delta': delta})
            values = [str([round(data[key], 3) for key in keys]) for data in (expected, actual, delta)]
            fragments.append('<tr><td>' + label + '</td>' + ''.join('<td>' + html.escape(value) + '</td>' for value in values) + '</tr>')
        fragments.append('</table>')
        fragments.append('<p>Windows Malgun font file present=' + str(reference['malgunFontFilePresent']).lower() + ' · 브라우저 ' + html.escape(reference['browser']['product']) + ' · Qt 헤더 폰트 <code>' + html.escape(json.dumps(geometry.get('headingFonts', []), ensure_ascii=False)) + '</code></p>')
        measurements[str(width)] = rows
    ordinary = args.native / 'ordinary'
    if ordinary.is_dir():
        fragments.append('<h2>실제 Windows 데스크톱: 일반 실행</h2><p>별도 통합 시나리오의 재배치 없이 실행했다. 창 테두리의 화면 포함, 공통 툴바 전체 가시 영역, 실제 마우스 입력 대상 및 확대 버튼의 결과를 검사한다. OS 배율 변경을 뜻하지 않는 Qt 프로세스 배율이다.</p>')
        for scale in ('1', '1.25', '1.5'):
            fragments.append(f'<h3>Qt {float(scale) * 100:g}%</h3><div class="captures">')
            for mode, label in (('empty', '빈 창'), ('three-pdfs', '3개 PDF'), ('restart', '설정 저장 후 재시작')):
                path = ordinary / f'{scale}-{mode}.json.desktop.png'
                data = json.loads((ordinary / f'{scale}-{mode}.json').read_text(encoding='utf-8-sig'))
                encoded = base64.b64encode(path.read_bytes()).decode()
                fragments.append('<figure><h3>' + label + ' · accepted=' + str(data['accepted']).lower() + '</h3><img src="data:image/png;base64,' + encoded + '"></figure>')
            fragments.append('</div>')
    fragments.append('''<h2>남은 차이와 기능을 위한 차이</h2>
<ul><li>Windows 기본 창 테두리와 PDF4QT 고급 메뉴는 유지한다. HTML의 시안 편집·초기화 버튼을 장식용 기능으로 추가하지 않는다.</li>
<li>폴더·논문·날짜·저자는 실제 데이터다. 시안의 예시 정보와 개수는 앱에 복사하지 않았다.</li>
<li>PDF 페이지 바깥 캔버스는 HTML의 #DFE5ED로 맞췄다. PDF 본문의 색상 처리·렌더링 품질은 유지한다.</li>
<li>실제 PDF 크기·기본 200% 배율·페이지 비율을 유지한다. HTML의 가변 폭 모형 페이지와 흰 페이지 크기/여백은 완전히 같지 않다.</li>
<li>Qt와 Chromium의 글꼴 래스터화, 기본 입력창/스크롤바 및 모서리 클리핑에는 차이가 남을 수 있다. 전체 유사도 수치나 완성도 백분율은 측정하지 않았다.</li></ul></html>''')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(fragments), encoding='utf-8')
    args.output.with_suffix('.geometry.json').write_text(json.dumps(measurements, ensure_ascii=False, indent=2), encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
