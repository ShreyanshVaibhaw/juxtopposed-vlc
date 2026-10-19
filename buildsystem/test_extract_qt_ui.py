#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import gettext
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('extract_qt_ui', Path(__file__).with_name('extract-qt-ui.py'))
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)


class QtUiExtraction(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.folder = self.root / 'modules/gui/qt/dialogs/messages'
        self.folder.mkdir(parents=True)

    def form(self, text):
        (self.folder / 'messages.ui').write_text(text, encoding='utf-8')

    def test_visible_text_notes_and_locations(self):
        self.form('''<ui>
<string extracomment="A translator note">&amp;Filter: %1</string>
<string>Français العربية 日本語</string>
<string>&lt;b&gt;Keep &quot;quotes&quot;&lt;/b&gt;\nNext line</string>
<string notr="true">not a translation</string>
<string notr="1">not a translation either</string>
<cstring>widgetObjectName</cstring>
<string></string>
</ui>''')
        text = extract.markers(self.root)
        self.assertIn('#line 2 "modules/gui/qt/dialogs/messages/messages.ui"', text)
        self.assertIn('// xgettext: A translator note', text)
        self.assertIn('QT_TR_NOOP("&Filter: %1");', text)
        self.assertIn('Français العربية 日本語', text)
        self.assertIn(r'QT_TR_NOOP("<b>Keep \"quotes\"</b>\nNext line");', text)
        self.assertNotIn('not a translation', text)
        self.assertNotIn('widgetObjectName', text)
        self.assertNotIn('QT_TR_NOOP("");', text)
        self.assertEqual(text, extract.markers(self.root))

    def test_does_not_read_external_entities(self):
        secret = self.root / 'private.txt'
        secret.write_text('must not be read', encoding='utf-8')
        self.form(f'<!DOCTYPE ui [<!ENTITY external SYSTEM "{secret.as_uri()}">]><ui><string>&external;</string></ui>')
        self.assertNotIn('must not be read', extract.markers(self.root))

    @unittest.skipUnless(shutil.which('xgettext') and shutil.which('msgmerge') and shutil.which('msgfmt'), 'GNU gettext tools required')
    def test_real_catalog_packaging_keeps_existing_translation(self):
        self.form('<ui><string>&amp;Verbosity:</string><string>%1 item</string></ui>')
        header = self.root / 'qt-ui-strings.h'
        header.write_text(extract.markers(self.root), encoding='utf-8')
        template = self.root / 'messages.pot'
        subprocess.run(['xgettext', '--language=C++', '--from-code=UTF-8', '--qt', '--keyword=QT_TR_NOOP:1', '--flag=QT_TR_NOOP:1:qt-format', '-o', str(template), str(header)], check=True)
        source = self.root / 'es.po'
        source.write_text('msgid ""\nmsgstr "Content-Type: text/plain; charset=UTF-8\\n"\n\nmsgid "&Verbosity:"\nmsgstr "&Verbosidad:"\n', encoding='utf-8')
        merged = self.root / 'merged.po'
        subprocess.run(['msgmerge', '--quiet', '--for-msgfmt', str(source), str(template), '-o', str(merged)], check=True)
        catalog = self.root / 'es.mo'
        subprocess.run(['msgfmt', '--check-format', str(merged), '-o', str(catalog)], check=True)
        with catalog.open('rb') as stream:
            self.assertEqual(gettext.GNUTranslations(stream).gettext('&Verbosity:'), '&Verbosidad:')
        self.assertIn('qt-format', template.read_text())


if __name__ == '__main__':
    unittest.main()
