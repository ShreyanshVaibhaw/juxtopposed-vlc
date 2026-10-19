#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Expose Qt Designer text to gettext without requiring generated UIC headers."""
import argparse
import json
from pathlib import Path
import sys
import xml.sax
from xml.sax.handler import ContentHandler, feature_external_ges, feature_external_pes


class Strings(ContentHandler):
    def __init__(self):
        super().__init__()
        self.messages = []
        self.depth = 0
        self.active = None

    def setDocumentLocator(self, locator):
        self.locator = locator

    def startElement(self, name, attrs):
        self.depth += 1
        if name == 'string' and attrs.get('notr', '').lower() not in ('true', '1'):
            self.active = (self.depth, self.locator.getLineNumber(), attrs.get('extracomment', ''), [])

    def characters(self, text):
        if self.active:
            self.active[3].append(text)

    def endElement(self, name):
        if self.active and self.active[0] == self.depth:
            _, line, note, parts = self.active
            text = ''.join(parts)
            if text:
                self.messages.append((line, text, note))
            self.active = None
        self.depth -= 1


def ui_files(root):
    return sorted((root / 'modules/gui/qt').rglob('*.ui'))


def markers(root):
    result = ['/* Generated Qt Designer gettext markers. Do not compile. */']
    for path in ui_files(root):
        handler = Strings()
        parser = xml.sax.make_parser()
        parser.setFeature(feature_external_ges, False)
        parser.setFeature(feature_external_pes, False)
        parser.setContentHandler(handler)
        parser.parse(str(path))
        for line, text, note in handler.messages:
            result.append('#line {} {}'.format(line, json.dumps(path.relative_to(root).as_posix())))
            for comment in note.splitlines():
                result.append('// xgettext: ' + comment)
            result.append('QT_TR_NOOP({});'.format(json.dumps(text, ensure_ascii=False)))
    return '\n'.join(result) + '\n'


def main():
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--root', type=Path, required=True)
    output = arguments.add_mutually_exclusive_group(required=True)
    output.add_argument('--list', action='store_true')
    output.add_argument('--output', type=Path)
    args = arguments.parse_args()
    root = args.root.resolve()
    if not (root / "modules/gui/qt").is_dir():
        arguments.error("Qt source directory is missing")
    if args.list:
        for path in ui_files(root):
            print(path)
        return
    text = markers(root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != text:
        args.output.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    main()
