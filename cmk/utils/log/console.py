#!/usr/bin/env python3
# Copyright (C) 2019 Checkmk GmbH - License: GNU General Public License v2
# This file is part of Checkmk (https://checkmk.com). It is subject to the terms and
# conditions defined in the file COPYING, which is part of this source code package.

import logging
from typing import TextIO

import cmk.utils.tty as tty

from ._level import VERBOSE as VERBOSE


def format_warning(text: str) -> str:
    stripped = text.lstrip()
    indent = text[: len(text) - len(stripped)]
    return f"{indent}{tty.bold}{tty.yellow}WARNING:{tty.normal} {stripped}"


def debug(text: str, *, file: TextIO | None = None) -> None:
    _print(logging.DEBUG, text, file=file)


def verbose(text: str, *, file: TextIO | None = None) -> None:
    _print(VERBOSE, text, file=file)


def info(text: str, *, file: TextIO | None = None) -> None:
    _print(logging.INFO, text, file=file)


def warning(text: str, *, file: TextIO | None = None) -> None:
    _print(logging.WARNING, text, file=file)


def error(text: str, *, file: TextIO | None = None) -> None:
    _print(logging.ERROR, text, file=file)


# NOTE: We abuse the log level of this logger as a global variable!
_console = logging.getLogger("cmk.base.console")


def _print(level: int, text: str, *, file: TextIO | None = None) -> None:
    if _console.isEnabledFor(level):
        print(text, end="", file=file, flush=True)
