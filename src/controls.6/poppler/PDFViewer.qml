import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.mauikit.controls as Maui
import org.mauikit.documents as Poppler

/**
 *  org.mauikit.controls.Page
 *  Displays, navigates, searches, and selects text in a PDF document.
 *
 * Set path or call open() to load a document. currentPage, totalPages, tocModel,
 * nextPage(), previousPage(), and goTo() provide navigation. pageScale and
 * zoomBy() control magnification. search() maintains the current result state,
 * while selectedText, clearSelection(), and copySelection() expose selection.
 *
 * The supportsForms, supportsAnnotations, and supportsSavingChanges capability
 * flags are currently false, and saveChanges() therefore performs no write.
 */
Maui.Page
{
    id: control

    property int currentPage : _listView.currentIndex
    property alias currentItem : _listView.currentItem
    property alias orientation : _listView.orientation
    property alias path : poppler.path
    property alias document : poppler
    property color searchHighlightColor: Qt.rgba(1, 1, .2, .4)

    // Capability flags for host apps such as Shelf.
    readonly property bool supportsForms: false
    readonly property bool supportsAnnotations: false
    readonly property bool supportsSavingChanges: false
    readonly property bool hasPendingChanges: false

    // Host apps can keep their own search UI and drive the shared viewer API.
    property bool showSearchControls: true
    property bool searchVisible: false

    property real pageScale: 1.0

    readonly property int totalPages: poppler.pages
    readonly property var tocModel: poppler.tocModel
    readonly property string selectedText: currentItem ? currentItem.selectedText : ""
    readonly property bool hasSelection: currentItem ? currentItem.hasSelection : false
    readonly property rect selectionRect: currentItem ? currentItem.selectionRect : Qt.rect(0, 0, 0, 0)
    readonly property rect selectionPageRect: currentItem ? currentItem.selectionPageRect : Qt.rect(0, 0, 0, 0)
    readonly property string currentSearchTerm: __currentSearchTerm
    readonly property int currentSearchResultIndex: __currentSearchResultIndex
    readonly property int searchResultsCount: __currentSearchResults.length

    onOrientationChanged: _relayoutCurrentPage()

    Connections
    {
        target: _listView
        function onWidthChanged() { control._relayoutCurrentPage() }
        function onHeightChanged() { control._relayoutCurrentPage() }
    }

    property bool enableLassoSelection : true
    property bool spacePressed: false

    signal areaClicked(var mouse)
    signal areaRightClicked()
    signal areaSelected(var rect)

    focus: true
    Keys.enabled: true
    Keys.onPressed: (event) =>
    {
        if (event.key === Qt.Key_Space)
        {
            control.spacePressed = true
            event.accepted = true
        }
    }
    Keys.onReleased: (event) =>
    {
        if (event.key === Qt.Key_Space)
        {
            control.spacePressed = false
            event.accepted = true
        }
    }

    headBar.visible: true
    footBar.visible: true
    footBar.forceCenterMiddleContent: true
    title: poppler.title
    padding: 0

    Maui.InputDialog
    {
        id: _passwordDialog

        title: i18n("Document Locked")
        message: i18n("Please enter your password to unlock and open the file.")
        textEntry.echoMode: TextInput.Password
        onFinished: poppler.unlock(text, text)
    }

    footerColumn: Maui.ToolBar
    {
        visible: control.showSearchControls && control.searchVisible
        width: parent ? parent.width : 0

        middleContent: Maui.SearchField
        {
            id: _searchField
            Layout.fillWidth: true
            Layout.maximumWidth: 500
            Layout.alignment: Qt.AlignHCenter

            onAccepted:
            {
                search(text)
            }

            onCleared:
            {
                control.__currentSearchTerm = ''
                control.__currentSearchResultIndex = -1
                control.__currentSearchResults = []
            }

            actions: [
                Action
                {
                    text: i18n("Case sensitive")
                    checkable: true
                    icon.name: "format-text-uppercase"
                    checked: searchSensitivity === Qt.CaseSensitive
                    onTriggered:
                    {
                        searchSensitivity = checked ? Qt.CaseSensitive : Qt.CaseInsensitive
                    }
                }
            ]
        }
    }

    footBar.rightContent: ToolButton
    {
        visible: control.showSearchControls
        icon.name: "search"
        checkable: true
        checked: control.searchVisible
        Maui.Controls.toolTipText: checked ? i18n("Hide search bar") : i18n("Search in document")
        onToggled: control.searchVisible = checked
    }

    Maui.ListBrowser
    {
        id: _listView
        anchors.fill: parent
        clip: true

        model: Poppler.Document
        {
            id: poppler

            property bool isLoading: true

            onPathChanged:
            {
                control.pageScale = 1.0
                _listView.contentX = 0
                _listView.contentY = 0
                _listView.currentIndex = 0
            }

            onPagesLoaded:
            {
                isLoading = false
            }

            onDocumentLocked: _passwordDialog.open()
        }

        orientation: ListView.Vertical
        snapMode: control.pageScale === 1.0 ? ListView.SnapOneItem : ListView.NoSnap
        flickable.onContentXChanged: control._updateCurrentPage()
        flickable.onContentYChanged: control._updateCurrentPage()

        delegate: Item
        {
            id: pageImg

            property bool panning: false
            property real panLastX: 0
            property real panLastY: 0

            readonly property real fitScale: Math.min(ListView.view.width / model.width,
                                                      ListView.view.height / model.height)
            readonly property real pageWidth: model.width * fitScale * control.pageScale
            readonly property real pageHeight: model.height * fitScale * control.pageScale
            readonly property real pageOffsetY: pageSurface.y

            width: Math.max(ListView.view.width, pageWidth)
            height: Math.max(ListView.view.height, pageHeight)
            readonly property int page: index

            function clamp(value, minValue, maxValue)
            {
                return Math.max(minValue, Math.min(maxValue, value))
            }

            function shouldPan(mouse)
            {
                return control.pageScale > 1.0
                    && (mouse.button === Qt.MiddleButton || (control.spacePressed && mouse.button === Qt.LeftButton))
            }

            function panBy(deltaX, deltaY)
            {
                const view = ListView.view
                const contentWidth = Math.max(view.contentWidth, pageImg.x + pageImg.width)
                if (view.contentWidth < contentWidth)
                    view.contentWidth = contentWidth
                const maxX = Math.max(0, contentWidth - view.width)
                const maxY = Math.max(0, view.contentHeight - view.height)
                view.contentX = clamp(view.contentX - deltaX, 0, maxX)
                view.contentY = clamp(view.contentY - deltaY, 0, maxY)
            }

            property alias selectionLayer: selectLayer

            readonly property var links: model.links
            readonly property string selectedText: _menu.selectedText
            readonly property bool hasSelection: selectLayer.visible && selectLayer.width > 0 && selectLayer.height > 0
            readonly property rect selectionRect: Qt.rect(selectLayer.x, selectLayer.y, selectLayer.width, selectLayer.height)
            readonly property rect selectionPageRect: Qt.rect(
                                                    pageImage.paintedWidth > 0 ? selectLayer.x / pageImage.paintedWidth : 0,
                                                    pageImage.paintedHeight > 0 ? selectLayer.y / pageImage.paintedHeight : 0,
                                                    pageImage.paintedWidth > 0 ? selectLayer.width / pageImage.paintedWidth : 0,
                                                    pageImage.paintedHeight > 0 ? selectLayer.height / pageImage.paintedHeight : 0)

            Item
            {
                id: pageSurface

                x: (pageImg.width - width) / 2
                y: pageImg.height > height ? (pageImg.height - height) / 2 : 0
                width: pageImg.pageWidth
                height: pageImg.pageHeight

                Image
                {
                    id: pageImage

                    anchors.fill: parent
                    asynchronous: true
                    cache: false
                    autoTransform: true
                    fillMode: Image.PreserveAspectFit
                    source: model.url
                    sourceSize.width: model.width * (1000 / model.width)
                    sourceSize.height: model.height * (1000 / model.height)
                }

                Maui.ProgressIndicator
                {
                    width: parent.width
                    anchors.bottom: parent.bottom
                    visible: pageImage.status === Image.Loading
                }
            }

            WheelHandler
            {
                target: null
                acceptedModifiers: Qt.ControlModifier

                onWheel: (event) =>
                {
                    const steps = event.angleDelta.y !== 0
                        ? event.angleDelta.y / 120
                        : event.pixelDelta.y / 15
                    control.zoomBy(steps * 0.1)
                    event.accepted = true
                }
            }

            MouseArea
            {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                propagateComposedEvents: true
                preventStealing: true
                scrollGestureEnabled: false
                cursorShape: pageImg.panning ? Qt.ClosedHandCursor : ((control.spacePressed && control.pageScale > 1.0) ? Qt.OpenHandCursor : Qt.ArrowCursor)

                onPressed: (mouse) =>
                {
                    if (pageImg.shouldPan(mouse))
                    {
                        pageImg.panning = true
                        // Use global coordinates so the delta is immune to the
                        // outer ListView moving this delegate on screen.
                        const g = mapToGlobal(mouse.x, mouse.y)
                        pageImg.panLastX = g.x
                        pageImg.panLastY = g.y
                        mouse.accepted = true
                    }
                    else
                    {
                        mouse.accepted = false
                    }
                }

                onPositionChanged: (mouse) =>
                {
                    if (!pageImg.panning)
                    {
                        mouse.accepted = false
                        return
                    }

                    const g = mapToGlobal(mouse.x, mouse.y)
                    pageImg.panBy(g.x - pageImg.panLastX, g.y - pageImg.panLastY)
                    pageImg.panLastX = g.x
                    pageImg.panLastY = g.y
                    mouse.accepted = true
                }

                onReleased: (mouse) =>
                {
                    if (!pageImg.panning)
                    {
                        mouse.accepted = false
                        return
                    }

                    pageImg.panning = false
                    mouse.accepted = true
                }

                onCanceled: pageImg.panning = false
                onClicked: (mouse) => mouse.accepted = pageImg.shouldPan(mouse)
            }

            Repeater
            {
                model: links
                delegate: MouseArea
                {
                    x: pageSurface.x + Math.round(modelData.rect.x * pageSurface.width)
                    y: pageSurface.y + Math.round(modelData.rect.y * pageSurface.height)
                    width: Math.round(modelData.rect.width * pageSurface.width)
                    height: Math.round(modelData.rect.height * pageSurface.height)

                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.goTo(modelData.destination)
                }
            }

            Item
            {
                id: _selectionLayer

                property alias selectionLayer: selectLayer

                parent: pageSurface
                anchors.fill: parent

                MouseArea
                {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    propagateComposedEvents: true
                    preventStealing: false

                    onPressed: (mouse) => mouse.accepted = false
                    onReleased: (mouse) => mouse.accepted = false
                    onClicked: (mouse) => mouse.accepted = false
                    onPressAndHold: (mouse) => mouse.accepted = false
                    onDoubleClicked: (mouse) => mouse.accepted = true
                }

                Rectangle
                {
                    visible: __currentSearchResult.page === index
                    color: control.searchHighlightColor
                    x: Math.round(__currentSearchResult.rect.x * pageImage.paintedWidth)
                    y: Math.round(__currentSearchResult.rect.y * pageImage.paintedHeight)
                    width: Math.round(__currentSearchResult.rect.width * pageImage.paintedWidth)
                    height: Math.round(__currentSearchResult.rect.height * pageImage.paintedHeight)
                }

                Maui.ContextualMenu
                {
                    id: _menu
                    property string selectedText

                    MenuItem
                    {
                        icon.name: "edit-copy"
                        text: i18n("Copy")
                        enabled: _menu.selectedText.length
                        onTriggered: Maui.Handy.copyTextToClipboard(_menu.selectedText)
                    }

                    MenuItem
                    {
                        text: i18nd("mauikitdocuments", "Search Selected Text on Google...")
                        enabled: _menu.selectedText.length
                        onTriggered: Qt.openUrlExternally("https://www.google.com/search?q=" + _menu.selectedText)
                    }

                    onClosed: selectLayer.reset()
                }

                Loader
                {
                    asynchronous: true
                    active: control.enableLassoSelection && pageImg.ListView.isCurrentItem
                    anchors.fill: parent
                    clip: false

                    sourceComponent: MouseArea
                    {
                        id: _mouseArea

                        propagateComposedEvents: true
                        preventStealing: true
                        acceptedButtons: Qt.RightButton | Qt.LeftButton
                        scrollGestureEnabled: false

                        onClicked: (mouse) =>
                        {
                            control.areaClicked(mouse)
                            control.forceActiveFocus()

                            if (mouse.button === Qt.RightButton)
                            {
                                control.areaRightClicked()
                                return
                            }
                        }


                        onPositionChanged: (mouse) =>
                        {
                            if (_mouseArea.pressed && control.enableLassoSelection && selectLayer.visible)
                            {
                                if (mouseX >= selectLayer.newX)
                                {
                                    selectLayer.width = (mouseX + 10) < (control.x + control.width) ? (mouseX - selectLayer.x) : selectLayer.width
                                } else {
                                    selectLayer.x = mouseX < control.x ? control.x : mouseX
                                    selectLayer.width = selectLayer.newX - selectLayer.x
                                }

                                if (mouseY >= selectLayer.newY) {
                                    selectLayer.height = (mouseY + 10) < (control.y + control.height) ? (mouseY - selectLayer.y) : selectLayer.height
                                } else {
                                    selectLayer.y = mouseY < control.y ? control.y : mouseY
                                    selectLayer.height = selectLayer.newY - selectLayer.y
                                }
                            }
                        }

                        onPressed: (mouse) =>
                        {
                            control.forceActiveFocus()

                            // Don't start a lasso selection if the pan MA already
                            // claimed this press – pressed events always propagate
                            // regardless of propagateComposedEvents, so we guard here.
                            if (pageImg.panning)
                            {
                                mouse.accepted = false
                                return
                            }

                            if (mouse.source === Qt.MouseEventNotSynthesized)
                            {
                                if (control.enableLassoSelection && mouse.button === Qt.LeftButton)
                                {
                                    selectLayer.visible = true
                                    selectLayer.x = mouseX
                                    selectLayer.y = mouseY
                                    selectLayer.newX = mouseX
                                    selectLayer.newY = mouseY
                                    selectLayer.width = 0
                                    selectLayer.height = 0
                                }
                            }
                        }

                        onPressAndHold: (mouse) =>
                        {
                            if (mouse.source !== Qt.MouseEventNotSynthesized && control.enableLassoSelection && !selectLayer.visible)
                            {
                                selectLayer.visible = true
                                selectLayer.x = mouseX
                                selectLayer.y = mouseY
                                selectLayer.newX = mouseX
                                selectLayer.newY = mouseY
                                selectLayer.width = 0
                                selectLayer.height = 0
                                mouse.accepted = true
                            } else {
                                mouse.accepted = false
                            }
                        }

                        onReleased: (mouse) =>
                        {
                            if (mouse.button !== Qt.LeftButton || !control.enableLassoSelection || !selectLayer.visible)
                            {
                                mouse.accepted = false
                                return
                            }

                            control.areaSelected(Qt.rect(selectLayer.x, selectLayer.y, selectLayer.width, selectLayer.height))

                            if (selectLayer.width > 0 && selectLayer.height > 0)
                            {
                                _menu.selectedText = poppler.getText(
                                    Qt.rect(selectLayer.x, selectLayer.y, selectLayer.width, selectLayer.height),
                                    Qt.size(pageImage.paintedWidth, pageImage.paintedHeight),
                                    pageImg.page)
                                _menu.show()
                            }
                        }
                    }
                }

                Label
                {
                    visible: selectLayer.width > 0 && selectLayer.height > 0 && selectLayer.visible && !_menu.visible
                    Maui.Theme.colorSet: Maui.Theme.Complementary
                    Maui.Theme.inherit: false
                    padding: Maui.Style.defaultPadding
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    anchors.bottom: selectLayer.top
                    anchors.margins: Maui.Style.space.big
                    anchors.left: selectLayer.left

                    text: poppler.getText(
                        Qt.rect(selectLayer.x, selectLayer.y, selectLayer.width, selectLayer.height),
                        Qt.size(pageImage.paintedWidth, pageImage.paintedHeight),
                        pageImg.page)

                    background: Rectangle
                    {
                        color: Maui.Theme.backgroundColor
                        radius: Maui.Style.radiusV
                    }
                }

                Maui.Rectangle
                {
                    id: selectLayer
                    property int newX: 0
                    property int newY: 0
                    height: 0
                    width: 0
                    x: 0
                    y: 0
                    visible: false
                    color: Qt.rgba(control.Maui.Theme.highlightColor.r, control.Maui.Theme.highlightColor.g, control.Maui.Theme.highlightColor.b, 0.2)
                    opacity: 0.7
                    borderColor: control.Maui.Theme.highlightColor
                    borderWidth: 2
                    solidBorder: false

                    function reset()
                    {
                        selectLayer.x = 0
                        selectLayer.y = 0
                        selectLayer.newX = 0
                        selectLayer.newY = 0
                        selectLayer.visible = false
                        selectLayer.width = 0
                        selectLayer.height = 0
                    }
                }
            }
        }
    }

    Maui.Holder
    {
        visible: !poppler.isValid
        anchors.fill: parent
        emoji: poppler.isLocked ? "qrc:/img_assets/assets/lock.svg" : "qrc:/img_assets/assets/alarm.svg"
        title: poppler.isLocked ? i18n("Locked") : i18n("Error")
        body: poppler.isLocked ? i18n("This document is password protected.") : i18n("There has been an error loading this document.")

        actions: Action
        {
            enabled: poppler.isLocked
            text: i18n("UnLock")
            onTriggered: _passwordDialog.open()
        }
    }

    function open(filePath)
    {
        poppler.path = filePath
    }

    function clearSelection()
    {
        const page = _listView.flickable.currentItem
        if (page && page.selectionLayer)
            page.selectionLayer.reset()
    }

    function copySelection()
    {
        if (selectedText.length > 0)
            Maui.Handy.copyTextToClipboard(selectedText)
    }

    function saveChanges()
    {
        return false
    }

    function goTo(destination)
    {
        _listView.currentIndex = destination.page
        _listView.flickable.positionViewAtIndex(destination.page, ListView.Beginning)
    }

    function zoomBy(delta)
    {
        const oldScale = pageScale
        const newScale = Math.max(1.0, Math.min(4.0, oldScale + delta))
        if (Math.abs(newScale - oldScale) < 0.001)
            return

        const view = _listView.flickable
        const anchorX = view.width / 2
        const anchorY = view.height / 2
        const page = view.itemAt(view.contentX + anchorX, view.contentY + anchorY)
        const pageIndex = page ? page.page : view.currentIndex
        const pagePositionX = page && page.width > 0
            ? (view.contentX + anchorX - page.x) / page.width
            : 0.5
        const pagePositionY = page && page.height > 0
            ? (view.contentY + anchorY - page.y) / page.height
            : 0.5

        pageScale = newScale

        Qt.callLater(function()
        {
            view.forceLayout()

            const resizedPage = view.itemAtIndex(pageIndex)
            if (resizedPage)
            {
                const contentWidth = Math.max(view.contentWidth, resizedPage.x + resizedPage.width)
                if (view.contentWidth < contentWidth)
                    view.contentWidth = contentWidth
                const maxX = Math.max(0, contentWidth - view.width)
                const maxY = Math.max(0, view.contentHeight - view.height)
                view.contentX = Math.max(0, Math.min(resizedPage.x + pagePositionX * resizedPage.width - anchorX, maxX))
                view.contentY = Math.max(0, Math.min(resizedPage.y + pagePositionY * resizedPage.height - anchorY, maxY))
            }

            control._updateCurrentPage()
        })
    }

    function _updateCurrentPage()
    {
        const view = _listView.flickable
        const index = view.indexAt(view.contentX + view.width / 2,
                                   view.contentY + view.height / 2)
        if (index >= 0 && index !== view.currentIndex)
            view.currentIndex = index
    }

    function _relayoutCurrentPage()
    {
        const view = _listView.flickable
        const anchorX = view.contentX + view.width / 2
        const anchorY = view.contentY + view.height / 2
        const pageItem = view.itemAt(anchorX, anchorY) || view.currentItem
        const page = pageItem ? pageItem.page : Math.max(0, view.currentIndex)
        const pagePositionX = pageItem && pageItem.width > 0
            ? (anchorX - pageItem.x) / pageItem.width
            : 0.5
        const pagePositionY = pageItem && pageItem.height > 0
            ? (anchorY - pageItem.y) / pageItem.height
            : 0.5

        Qt.callLater(function()
        {
            view.forceLayout()

            const resizedPage = view.itemAtIndex(page)
            if (resizedPage)
            {
                const contentWidth = Math.max(view.contentWidth, resizedPage.x + resizedPage.width)
                if (view.contentWidth < contentWidth)
                    view.contentWidth = contentWidth
                const maxX = Math.max(0, contentWidth - view.width)
                const maxY = Math.max(0, view.contentHeight - view.height)
                view.contentX = Math.max(0, Math.min(resizedPage.x + pagePositionX * resizedPage.width - view.width / 2, maxX))
                view.contentY = Math.max(0, Math.min(resizedPage.y + pagePositionY * resizedPage.height - view.height / 2, maxY))
            }
            else
            {
                view.positionViewAtIndex(page, ListView.Beginning)
            }

            control._updateCurrentPage()
        })
    }

    signal searchNotFound
    signal searchRestartedFromTheBeginning

    property int searchSensitivity: Qt.CaseInsensitive
    property string __currentSearchTerm
    property int __currentSearchResultIndex: -1
    property var __currentSearchResults: []
    property var __currentSearchResult: __currentSearchResultIndex > -1
                                        ? __currentSearchResults[__currentSearchResultIndex]
                                        : { page: -1, rect: Qt.rect(0, 0, 0, 0) }

    function search(text)
    {
        if (!poppler.isValid)
            return

        if (text.length === 0)
        {
            __currentSearchTerm = ''
            __currentSearchResultIndex = -1
            __currentSearchResults = []
        } else if (text === __currentSearchTerm)
        {
            if (__currentSearchResultIndex < __currentSearchResults.length - 1)
            {
                __currentSearchResultIndex++
                __scrollTo(__currentSearchResult)
            } else {
                var page = __currentSearchResult.page
                __currentSearchResultIndex = -1
                __currentSearchResults = []
                if (page < _listView.count - 1)
                {
                    __search(page + 1, __currentSearchTerm)
                } else {
                    control.searchRestartedFromTheBeginning()
                    __search(0, __currentSearchTerm)
                }
            }
        } else {
            __currentSearchTerm = text
            __currentSearchResultIndex = -1
            __currentSearchResults = []
            __search(currentPage, text)
        }
    }

    function __search(startPage, text)
    {
        if (startPage >= _listView.count)
            throw new Error('Start page index is larger than number of pages in document')

        function resultFound(page, result)
        {
            var searchResults = []
            for (var i = 0; i < result.length; ++i)
                searchResults.push({ page: page, rect: result[i] })
            __currentSearchResults = searchResults
            __currentSearchResultIndex = 0
            __scrollTo(__currentSearchResult)
        }

        var found = false
        for (var page = startPage; page < _listView.count; ++page)
        {
            var result = poppler.search(page, text, searchSensitivity)
            if (result.length > 0)
            {
                found = true
                resultFound(page, result)
                break
            }
        }

        if (!found)
        {
            for (page = 0; page < startPage; ++page)
            {
                result = poppler.search(page, text, searchSensitivity)
                if (result.length > 0)
                {
                    found = true
                    control.searchRestartedFromTheBeginning()
                    resultFound(page, result)
                    break
                }
            }
        }

        if (!found)
            control.searchNotFound()
    }

    function __scrollTo(destination)
    {
        goTo(destination)

        Qt.callLater(function()
        {
            const page = _listView.flickable.itemAtIndex(destination.page)
            if (!page)
                return

            const resultTop = page.y + page.pageOffsetY + Math.round(destination.rect.top * page.pageHeight)
            const resultBottom = page.y + page.pageOffsetY + Math.round(destination.rect.bottom * page.pageHeight)
            const visibleTop = _listView.contentY
            const visibleBottom = visibleTop + _listView.height

            if (resultBottom > visibleBottom)
                _listView.contentY += resultBottom - visibleBottom + _listView.spacing
            else if (resultTop < visibleTop)
                _listView.contentY = Math.max(0, resultTop - _listView.spacing)
        })
    }

    function previousPage()
    {
        if (_listView.currentIndex > 0)
            goTo({ page: _listView.currentIndex - 1, top: 0 })
    }

    function nextPage()
    {
        if (_listView.currentIndex + 1 < poppler.pages)
            goTo({ page: _listView.currentIndex + 1, top: 0 })
    }
}
