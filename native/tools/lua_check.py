# SPDX-License-Identifier: GPL-3.0-or-later
"""Light-weight Lua 5.3 structure checker (no Lua installation needed).

Checks string/comment termination, bracket balance and block keyword balance
(function/if/for/while/do/repeat vs end/until, then/elseif/else placement).
It is not a full parser, but catches the mistakes that usually happen while editing scripts.

usage: lua_check.py file.lua [file.lua ...]
"""

import re
import sys

KEYWORDS = {'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'goto', 'if',
            'in', 'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while'}


def tokenize(src):
    if src.startswith('﻿'):            # Lua's loader skips a UTF-8 BOM as well
        src = src[1:]
    i, n, line = 0, len(src), 1
    toks = []
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c in ' \t\r':
            i += 1
            continue
        if src.startswith('--', i):
            m = re.match(r'--\[(=*)\[', src[i:])
            if m:
                close = ']' + m.group(1) + ']'
                j = src.find(close, i + m.end())
                if j < 0:
                    raise SyntaxError('line %d: unterminated long comment' % line)
                line += src.count('\n', i, j)
                i = j + len(close)
            else:
                j = src.find('\n', i)
                i = n if j < 0 else j
            continue
        m = re.match(r'\[(=*)\[', src[i:])
        if m:
            close = ']' + m.group(1) + ']'
            j = src.find(close, i + m.end())
            if j < 0:
                raise SyntaxError('line %d: unterminated long string' % line)
            toks.append(('str', None, line))
            line += src.count('\n', i, j)
            i = j + len(close)
            continue
        if c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == '\\':
                    j += 1
                elif src[j] == '\n':
                    raise SyntaxError('line %d: unterminated string' % line)
                j += 1
            if j >= n:
                raise SyntaxError('line %d: unterminated string' % line)
            toks.append(('str', None, line))
            i = j + 1
            continue
        m = re.match(r'[A-Za-z_][A-Za-z0-9_]*', src[i:])
        if m:
            w = m.group(0)
            toks.append(('kw' if w in KEYWORDS else 'name', w, line))
            i += len(w)
            continue
        m = re.match(r'0[xX][0-9a-fA-F.]+([pP][+-]?\d+)?|\d+\.?\d*([eE][+-]?\d+)?|\.\d+([eE][+-]?\d+)?', src[i:])
        if m:
            toks.append(('num', m.group(0), line))
            i += len(m.group(0))
            continue
        m = re.match(r'\.\.\.|\.\.|==|~=|<=|>=|//|::|<<|>>|[-+*/%^#&~|<>=(){}\[\];:,.]', src[i:])
        if m:
            toks.append(('op', m.group(0), line))
            i += len(m.group(0))
            continue
        raise SyntaxError('line %d: unexpected character %r' % (line, c))
    return toks


def check(src):
    toks = tokenize(src)
    stack = []          # (kind, line)
    brackets = []
    pairs = {')': '(', ']': '[', '}': '{'}
    for idx, (kind, val, line) in enumerate(toks):
        if kind == 'op' and val in '([{':
            brackets.append((val, line))
        elif kind == 'op' and val in ')]}':
            if not brackets or brackets[-1][0] != pairs[val]:
                raise SyntaxError('line %d: unbalanced %s' % (line, val))
            brackets.pop()
        if kind != 'kw':
            continue
        if val == 'function':
            stack.append(('function', line))
        elif val == 'if':
            stack.append(('if', line))
        elif val in ('for', 'while'):
            stack.append((val, line))
        elif val == 'do':
            if stack and stack[-1][0] in ('for', 'while') and not stack[-1][0].endswith('_do'):
                stack[-1] = (stack[-1][0] + '_do', stack[-1][1])
            else:
                stack.append(('do', line))
        elif val == 'repeat':
            stack.append(('repeat', line))
        elif val == 'then':
            if not stack or stack[-1][0] not in ('if', 'elseif'):
                raise SyntaxError('line %d: "then" without if/elseif' % line)
            stack[-1] = ('if_then', stack[-1][1])
        elif val == 'elseif':
            if not stack or stack[-1][0] != 'if_then':
                raise SyntaxError('line %d: "elseif" outside if' % line)
            stack[-1] = ('elseif', stack[-1][1])
        elif val == 'else':
            if not stack or stack[-1][0] != 'if_then':
                raise SyntaxError('line %d: "else" outside if' % line)
            stack[-1] = ('if_else', stack[-1][1])
        elif val == 'end':
            if not stack or stack[-1][0] not in ('function', 'if_then', 'if_else', 'for_do', 'while_do', 'do'):
                top = stack[-1] if stack else ('<empty>', 0)
                raise SyntaxError('line %d: "end" does not close a block (open: %s from line %d)' % (line, top[0], top[1]))
            stack.pop()
        elif val == 'until':
            if not stack or stack[-1][0] != 'repeat':
                raise SyntaxError('line %d: "until" without repeat' % line)
            stack.pop()
    if brackets:
        raise SyntaxError('unclosed %s from line %d' % brackets[-1])
    if stack:
        raise SyntaxError('unclosed %s from line %d' % stack[-1])
    return len(toks)


def main():
    bad = 0
    for path in sys.argv[1:]:
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                n = check(f.read())
            print('OK    %s (%d tokens)' % (path, n))
        except SyntaxError as e:
            bad += 1
            print('FAIL  %s: %s' % (path, e))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
