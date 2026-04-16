import QtQuick
import QtQuick.Controls

import org.mauikit.controls as Maui
import org.mauikit.documents as Docs

Maui.Page
{
    id: control

    property string path
    property bool twoPagesMode: true
    property alias orientation: _listView.orientation
    readonly property int currentPage: _listView.currentIndex

    title: _model.title
    headBar.visible: false

    footBar.leftContent: ToolButton
    {
        icon.name: "view-dual-symbolic"
        checked: control.twoPagesMode
        onClicked: control.twoPagesMode = !control.twoPagesMode
    }

    ListView
    {
        id: _listView
        anchors.fill: parent
        orientation: ListView.Horizontal
        snapMode: control.twoPagesMode ? ListView.SnapPosition : ListView.SnapOneItem
        cacheBuffer: 3000

        onMovementEnded:
        {
            var indexHere = indexAt(contentX + width / 2, contentY + height / 2)
            if (currentIndex !== indexHere)
                currentIndex = indexHere
        }

        model: Docs.ArchiveBookModel
        {
            id: _model
            qmlEngine: globalQmlEngine
        }

        delegate: Maui.ImageViewer
        {
            source: model.url
            height: ListView.view.height
            width: Math.floor(ListView.view.width / (control.twoPagesMode ? 2 : 1))
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
    }

    onPathChanged:
    {
        if (path.length > 0)
            _model.filename = control.path.replace("file://", "")
    }
}
