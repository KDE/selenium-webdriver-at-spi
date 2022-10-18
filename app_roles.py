# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2021 Harald Sitter <sitter@kde.org>

import pyatspi

ROLE_NAMES = {
        pyatspi.ROLE_INVALID:'invalid',
        pyatspi.ROLE_ACCELERATOR_LABEL:'accelerator label',
        pyatspi.ROLE_ALERT:'alert',
        pyatspi.ROLE_ANIMATION:'animation',
        pyatspi.ROLE_ARROW:'arrow',
        pyatspi.ROLE_CALENDAR:'calendar',
        pyatspi.ROLE_CANVAS:'canvas',
        pyatspi.ROLE_CHECK_BOX:'check box',
        pyatspi.ROLE_CHECK_MENU_ITEM:'check menu item',
        pyatspi.ROLE_COLOR_CHOOSER:'color chooser',
        pyatspi.ROLE_COLUMN_HEADER:'column header',
        pyatspi.ROLE_COMBO_BOX:'combo box',
        pyatspi.ROLE_DATE_EDITOR:'dateeditor',
        pyatspi.ROLE_DESKTOP_ICON:'desktop icon',
        pyatspi.ROLE_DESKTOP_FRAME:'desktop frame',
        pyatspi.ROLE_DIAL:'dial',
        pyatspi.ROLE_DIALOG:'dialog',
        pyatspi.ROLE_DIRECTORY_PANE:'directory pane',
        pyatspi.ROLE_DRAWING_AREA:'drawing area',
        pyatspi.ROLE_FILE_CHOOSER:'file chooser',
        pyatspi.ROLE_FILLER:'filler',
        pyatspi.ROLE_FONT_CHOOSER:'font chooser',
        pyatspi.ROLE_FRAME:'frame',
        pyatspi.ROLE_GLASS_PANE:'glass pane',
        pyatspi.ROLE_HTML_CONTAINER:'html container',
        pyatspi.ROLE_ICON:'icon',
        pyatspi.ROLE_IMAGE:'image',
        pyatspi.ROLE_INTERNAL_FRAME:'internal frame',
        pyatspi.ROLE_LABEL:'label',
        pyatspi.ROLE_LAYERED_PANE:'layered pane',
        pyatspi.ROLE_LIST:'list',
        pyatspi.ROLE_LIST_ITEM:'list item',
        pyatspi.ROLE_MENU:'menu',
        pyatspi.ROLE_MENU_BAR:'menu bar',
        pyatspi.ROLE_MENU_ITEM:'menu item',
        pyatspi.ROLE_OPTION_PANE:'option pane',
        pyatspi.ROLE_PAGE_TAB:'page tab',
        pyatspi.ROLE_PAGE_TAB_LIST:'page tab list',
        pyatspi.ROLE_PANEL:'panel',
        pyatspi.ROLE_PASSWORD_TEXT:'password text',
        pyatspi.ROLE_POPUP_MENU:'popup menu',
        pyatspi.ROLE_PROGRESS_BAR:'progress bar',
        pyatspi.ROLE_PUSH_BUTTON:'push button',
        pyatspi.ROLE_RADIO_BUTTON:'radio button',
        pyatspi.ROLE_RADIO_MENU_ITEM:'radio menu item',
        pyatspi.ROLE_ROOT_PANE:'root pane',
        pyatspi.ROLE_ROW_HEADER:'row header',
        pyatspi.ROLE_SCROLL_BAR:'scroll bar',
        pyatspi.ROLE_SCROLL_PANE:'scroll pane',
        pyatspi.ROLE_SEPARATOR:'separator',
        pyatspi.ROLE_SLIDER:'slider',
        pyatspi.ROLE_SPLIT_PANE:'split pane',
        pyatspi.ROLE_SPIN_BUTTON:'spin button',
        pyatspi.ROLE_STATUS_BAR:'status bar',
        pyatspi.ROLE_TABLE:'table',
        pyatspi.ROLE_TABLE_CELL:'table cell',
        pyatspi.ROLE_TABLE_COLUMN_HEADER:'table column header',
        pyatspi.ROLE_TABLE_ROW_HEADER:'table row header',
        pyatspi.ROLE_TEAROFF_MENU_ITEM:'tear off menu item',
        pyatspi.ROLE_TERMINAL:'terminal',
        pyatspi.ROLE_TEXT:'text',
        pyatspi.ROLE_TOGGLE_BUTTON:'toggle button',
        pyatspi.ROLE_TOOL_BAR:'tool bar',
        pyatspi.ROLE_TOOL_TIP:'tool tip',
        pyatspi.ROLE_TREE:'tree',
        pyatspi.ROLE_TREE_TABLE:'tree table',
        pyatspi.ROLE_UNKNOWN:'unknown',
        pyatspi.ROLE_VIEWPORT:'viewport',
        pyatspi.ROLE_WINDOW:'window',
        pyatspi.ROLE_HEADER:'header',
        pyatspi.ROLE_FOOTER:'footer',
        pyatspi.ROLE_PARAGRAPH:'paragraph',
        pyatspi.ROLE_RULER:'ruler',
        pyatspi.ROLE_APPLICATION:'application',
        pyatspi.ROLE_AUTOCOMPLETE:'autocomplete',
        pyatspi.ROLE_EDITBAR:'edit bar',
        pyatspi.ROLE_EMBEDDED:'embedded component',
        pyatspi.ROLE_ENTRY:'entry',
        pyatspi.ROLE_CHART:'chart',
        pyatspi.ROLE_CAPTION:'caption',
        pyatspi.ROLE_DOCUMENT_FRAME:'document frame',
        pyatspi.ROLE_HEADING:'heading',
        pyatspi.ROLE_PAGE:'page',
        pyatspi.ROLE_SECTION:'section',
        pyatspi.ROLE_REDUNDANT_OBJECT:'redundant object',
        pyatspi.ROLE_FORM:'form',
        pyatspi.ROLE_LINK:'link',
        pyatspi.ROLE_INPUT_METHOD_WINDOW:'input method window',
        pyatspi.ROLE_TABLE_ROW:'table row',
        pyatspi.ROLE_TREE_ITEM:'tree item',
        pyatspi.ROLE_DOCUMENT_SPREADSHEET:'document spreadsheet',
        pyatspi.ROLE_DOCUMENT_PRESENTATION:'document presentation',
        pyatspi.ROLE_DOCUMENT_TEXT:'document text',
        pyatspi.ROLE_DOCUMENT_WEB:'document web',
        pyatspi.ROLE_DOCUMENT_EMAIL:'document email',
        pyatspi.ROLE_COMMENT:'comment',
        pyatspi.ROLE_LIST_BOX:'list box',
        pyatspi.ROLE_GROUPING:'grouping',
        pyatspi.ROLE_IMAGE_MAP:'image map',
        pyatspi.ROLE_NOTIFICATION:'notification',
        pyatspi.ROLE_INFO_BAR:'info bar',
        pyatspi.ROLE_LEVEL_BAR:'level bar',
        pyatspi.ROLE_TITLE_BAR:'title bar',
        pyatspi.ROLE_BLOCK_QUOTE:'block quote',
        pyatspi.ROLE_AUDIO:'audio',
        pyatspi.ROLE_VIDEO:'video',
        pyatspi.ROLE_DEFINITION:'definition',
        pyatspi.ROLE_ARTICLE:'article',
        pyatspi.ROLE_LANDMARK:'landmark',
        pyatspi.ROLE_LOG:'log',
        pyatspi.ROLE_MARQUEE:'marquee',
        pyatspi.ROLE_MATH:'math',
        pyatspi.ROLE_RATING:'rating',
        pyatspi.ROLE_TIMER:'timer',
        pyatspi.ROLE_STATIC:'static',
        pyatspi.ROLE_MATH_FRACTION:'math fraction',
        pyatspi.ROLE_MATH_ROOT: 'math root',
        pyatspi.ROLE_SUBSCRIPT: 'subscript',
        pyatspi.ROLE_SUPERSCRIPT: 'superscript',
        pyatspi.ROLE_CONTENT_DELETION: 'content deletion',
        pyatspi.ROLE_CONTENT_INSERTION: 'content insertion',
        pyatspi.ROLE_MARK: 'mark',
        pyatspi.ROLE_SUGGESTION: 'suggestion',
}