i18n = import('i18n')

add_project_arguments(f'-DGETTEXT_PACKAGE="@vlc_package_name@"',
	language: ['c', 'cpp', 'objc'])
qt_ui_python = find_program('python3', native: true)
qt_ui_extractor = files('../buildsystem/extract-qt-ui.py')
qt_ui_sources = run_command(qt_ui_python, qt_ui_extractor,
    '--root', meson.project_source_root(), '--list', check: true).stdout().strip().split('\n')
configure_file(
    input: files(qt_ui_sources),
    output: 'qt-ui-strings.h',
    command: [qt_ui_python, qt_ui_extractor, '--root', meson.project_source_root(), '--output', '@OUTPUT@'])
if not get_option('tests').disabled()
    test('qt-ui-gettext', qt_ui_python, args: files('../buildsystem/test_extract_qt_ui.py'))
endif

i18n.gettext(vlc_package_name,
	args: [
        '--directory=' + meson.project_build_root(),
		'--keyword=_',
		'--keyword=N_',
		'--keyword=_NS',
		'--keyword=qtr',
		'--keyword=qtr:1,2,3t',
		'--keyword=qfut:1',
		'--keyword=tr:1',
		'--keyword=tr:1,2,3t',
		'--keyword=QT_TR_NOOP:1',
		'--keyword=qsTr:1,1t',
		'--keyword=qsTr:1,2,3t',
		'--keyword=qsTranslate:1c,2',
		'--keyword=Q_',
		'--language=C++',
		'--qt',
		'--flag=qtr:1:qt-format',
		'--flag=qtr:2:qt-format',
		'--flag=qfut:1:qt-format',
		'--flag=tr:1:qt-format',
		'--flag=tr:2:qt-format',
		'--flag=QT_TR_NOOP:1:qt-format',
		'--flag=qsTr:1:qt-format',
		'--flag=qsTr:2:qt-format',
		'--flag=qsTranslate:2:qt-format',
		'--keyword=vlc_ngettext:1,2',
		'--keyword=vlc_pgettext:1c,2',
		'--keyword=_PNS:1c,2',
		'--add-comments=xgettext:',
		'--from-code=UTF-8',
	],
	install_dir: localedir_path)
