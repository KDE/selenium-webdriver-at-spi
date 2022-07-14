#!/bin/sh

# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2021 Harald Sitter <sitter@kde.org>

export FLASK_ENV=development
export FLASK_APP=app.py
flask run --port 4723
