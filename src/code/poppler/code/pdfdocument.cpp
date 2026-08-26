/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Anthony Granger <grangeranthony@gmail.com>
 *          Stefano Verzegnassi <stefano92.100@gmail.com>
 */

#include "pdfdocument.h"
#include "pdfimageprovider.h"
#include <poppler/qt6/poppler-version.h>
#include <poppler/qt6/poppler-form.h>

#include <QDebug>
#include <QFileInfo>
#include <QMetaType>
#include <QMutexLocker>
#include <QSaveFile>
#include <QQmlEngine>
#include <QQmlContext>
#include <QUuid>

#include <QtConcurrent/QtConcurrent>

static int InstanceCount = 0;

PdfDocument::PdfDocument(QAbstractListModel *parent):
    QAbstractListModel(parent)
  , m_path("")
  , m_id(QString("poppler-%1").arg(InstanceCount++))
  , m_providersNumber(1)
  , m_providerState(std::make_shared<PdfImageProviderState>())
  , m_tocModel(nullptr)
{
    // qRegisterMetaType<PdfPagesList>("PdfPagesList");
    qDebug() << "REGISTERING POPPLER DOCUMENT INSTANCE";
}

QHash<int, QByteArray> PdfDocument::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[WidthRole] = "width";
    roles[HeightRole] = "height";
    roles[UrlRole] = "url";
    roles[LinksRole] = "links";
    return roles;
}

int PdfDocument::rowCount(const QModelIndex & parent) const
{
    if (parent.isValid() || !m_document)
    {
        return 0;
    }

    if (m_document->isLocked())
    {
        return 0;
    }

    return m_document->numPages();
}

QVariant PdfDocument::data(const QModelIndex & index, int role) const
{
    if (!index.isValid() || !m_document)
        return QVariant();

    if (index.row() < 0 || index.row() > m_document->numPages())
        return QVariant();

    auto page = m_pages.at(index.row());

    switch (role)
    {
    case WidthRole:
        return page.width();
    case HeightRole:
        return page.height();
    case UrlRole:
        return page.url();
    case LinksRole:
        return page.links();
    default:
        return QVariant();
    }
}

void PdfDocument::setPath(QUrl &pathName)
{
    if(m_path == pathName || pathName.isEmpty())
    {
        return;
    }

    m_path = pathName;
    Q_EMIT pathChanged();

    if (!loadDocument(m_path.toLocalFile()))
        return;
}

int PdfDocument::pageCount() const
{
    return this->pages;
}

bool PdfDocument::loadDocument(const QString &pathName, const QString &password, const QString &userPassword)
{
    qDebug() << "Loading document...";

    if (pathName.isEmpty()) {
        qDebug() << "Can't load the document, path is empty.";
        return false;
    }

    if (m_modified)
    {
        m_modified = false;
        Q_EMIT modifiedChanged();
    }
    m_supportsForms = false;
    m_supportsAnnotations = false;
    m_supportsSavingChanges = false;
    Q_EMIT capabilitiesChanged();
    ++m_renderRevision;
    Q_EMIT renderRevisionChanged();

    if (m_providerState)
    {
        QMutexLocker locker(&m_providerState->mutex);
        m_providerState->document = nullptr;
        m_providerState->tileCache.clear();
    }

    {
        QMutexLocker popplerLocker(&popplerRenderMutex());
        m_document = Poppler::Document::load(pathName, password.toUtf8(), userPassword.toUtf8());

        if (!m_document)
        {
            qDebug() << "ERROR : Can't open the document located at " + pathName;
            Q_EMIT error("Can't open the document located at " + pathName);

            this->m_isValid = false;
            Q_EMIT this->isValidChanged();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            delete m_document;
#endif
            return false;
        }

        m_document->setRenderHint(Poppler::Document::Antialiasing, true);
        m_document->setRenderHint(Poppler::Document::TextAntialiasing, true);
        m_document->setRenderHint(Poppler::Document::HideAnnotations, false);

        if (m_document->isLocked())
        {
            qDebug() << "ERROR : Can't open the document located at beacuse it is locked" + pathName;
            Q_EMIT this->documentLocked();
            Q_EMIT this->isLockedChanged();

            this->m_isValid = false;
            Q_EMIT this->isValidChanged();

            return false;
        }

        this->pages = this->m_document->numPages();
        this->m_supportsForms = this->m_document->formType() == Poppler::Document::AcroForm;
        this->m_supportsAnnotations = true;
        this->m_supportsSavingChanges = true;
    }

    {
        QMutexLocker locker(&m_providerState->mutex);
        m_providerState->document = m_document.get();
    }

    qDebug() << "Document loaded successfully !";

    Q_EMIT this->pagesCountChanged();
    Q_EMIT this->titleChanged();
    Q_EMIT this->isLockedChanged();
    Q_EMIT capabilitiesChanged();

    this->m_isValid = true;
    Q_EMIT this->isValidChanged();

    // Init toc model
    if(!m_tocModel)
    {
        m_tocModel = new PdfTocModel(this);
    }

    m_tocModel->setDocument(m_document.get());
    Q_EMIT tocModelChanged();

    beginResetModel();
    loadPages();
    endResetModel();
    Q_EMIT formFieldsChanged();
    Q_EMIT annotationsChanged();

    return true;
}

QDateTime PdfDocument::getDocumentDate(QString data)
{
    if (!m_document)
        return QDateTime();

    if (data == "CreationDate" || data == "ModDate")
        return m_document->date(data);
    else
        return QDateTime();
}

QString PdfDocument::getDocumentInfo(QString data) const
{
    if (!m_document)
        return QString("");

    if (data == "Title" || data == "Subject" || data == "Author" || data == "Creator" || data == "Producer")
        return m_document->info(data);
    else
        return QString("");
}

QString PdfDocument::title() const
{
    if (!m_document)
        return QFileInfo(m_path.toLocalFile()).fileName();

    QString res = this->m_document->title();

    if(res.isEmpty())
    {
        res = QFileInfo(m_path.toLocalFile()).fileName();
    }

    return res;
}

bool PdfDocument::isLocked() const
{
    if(!m_document)
        return false;

    return m_document->isLocked();
}

bool PdfDocument::isValid() const
{
    return m_isValid;
}

QString PdfDocument::id() const
{
    return m_id;
}

static QString formFieldType(const Poppler::FormField *field)
{
    if (!field)
        return QString();

    switch (field->type())
    {
    case Poppler::FormField::FormText:
        return QStringLiteral("text");
    case Poppler::FormField::FormChoice:
    {
        const auto choice = dynamic_cast<const Poppler::FormFieldChoice *>(field);
        return choice && choice->choiceType() == Poppler::FormFieldChoice::ListBox
            ? QStringLiteral("listbox") : QStringLiteral("combobox");
    }
    case Poppler::FormField::FormButton:
    {
        const auto button = dynamic_cast<const Poppler::FormFieldButton *>(field);
        if (!button)
            return QStringLiteral("button");
        if (button->buttonType() == Poppler::FormFieldButton::CheckBox)
            return QStringLiteral("checkbox");
        if (button->buttonType() == Poppler::FormFieldButton::Radio)
            return QStringLiteral("radio");
        return QStringLiteral("button");
    }
    case Poppler::FormField::FormSignature:
        return QStringLiteral("signature");
    }

    return QString();
}

QVariantList PdfDocument::formFields() const
{
    QVariantList result;
    QMutexLocker popplerLocker(&popplerRenderMutex());
    if (!m_document)
        return result;

    for (int pageNumber = 0; pageNumber < m_document->numPages(); ++pageNumber)
    {
        const auto page = m_document->page(pageNumber);
        if (!page)
            continue;

        const auto fields = page->formFields();
        for (const auto &field : fields)
        {
            if (!field)
                continue;

            QVariantMap item{
                {QStringLiteral("page"), pageNumber},
                {QStringLiteral("id"), field->id()},
                {QStringLiteral("name"), field->name()},
                {QStringLiteral("qualifiedName"), field->fullyQualifiedName()},
                {QStringLiteral("uiName"), field->uiName()},
                {QStringLiteral("type"), formFieldType(field.get())},
                {QStringLiteral("rect"), field->rect()},
                {QStringLiteral("readOnly"), field->isReadOnly()},
                {QStringLiteral("visible"), field->isVisible()},
                {QStringLiteral("printable"), field->isPrintable()}
            };

            if (const auto text = dynamic_cast<const Poppler::FormFieldText *>(field.get()))
            {
                item[QStringLiteral("value")] = text->text();
                item[QStringLiteral("multiline")] = text->textType() == Poppler::FormFieldText::Multiline;
                item[QStringLiteral("password")] = text->isPassword();
                item[QStringLiteral("maximumLength")] = text->maximumLength();
            }
            else if (const auto choice = dynamic_cast<const Poppler::FormFieldChoice *>(field.get()))
            {
                QVariantList currentChoices;
                for (const int choiceIndex : choice->currentChoices())
                    currentChoices.append(choiceIndex);

                item[QStringLiteral("choices")] = choice->choices();
                item[QStringLiteral("currentChoices")] = currentChoices;
                item[QStringLiteral("editable")] = choice->isEditable();
                item[QStringLiteral("multiSelect")] = choice->multiSelect();
                item[QStringLiteral("value")] = choice->editChoice();
            }
            else if (const auto button = dynamic_cast<const Poppler::FormFieldButton *>(field.get()))
            {
                item[QStringLiteral("checked")] = button->state();
                item[QStringLiteral("caption")] = button->caption();
            }

            result.append(item);
        }
    }

    return result;
}

static QString annotationType(Poppler::Annotation::SubType type)
{
    switch (type)
    {
    case Poppler::Annotation::AText: return QStringLiteral("text");
    case Poppler::Annotation::ALine: return QStringLiteral("line");
    case Poppler::Annotation::AGeom: return QStringLiteral("geometry");
    case Poppler::Annotation::AHighlight: return QStringLiteral("highlight");
    case Poppler::Annotation::AStamp: return QStringLiteral("stamp");
    case Poppler::Annotation::AInk: return QStringLiteral("ink");
    case Poppler::Annotation::ACaret: return QStringLiteral("caret");
    case Poppler::Annotation::AFileAttachment: return QStringLiteral("file");
    case Poppler::Annotation::ASound: return QStringLiteral("sound");
    case Poppler::Annotation::AMovie: return QStringLiteral("movie");
    case Poppler::Annotation::AScreen: return QStringLiteral("screen");
    case Poppler::Annotation::AWidget: return QStringLiteral("widget");
    case Poppler::Annotation::ARichMedia: return QStringLiteral("richMedia");
    case Poppler::Annotation::ALink: return QStringLiteral("link");
    }

    return QStringLiteral("unknown");
}

QVariantList PdfDocument::annotations() const
{
    QVariantList result;
    QMutexLocker popplerLocker(&popplerRenderMutex());
    if (!m_document)
        return result;

    for (int pageNumber = 0; pageNumber < m_document->numPages(); ++pageNumber)
    {
        const auto page = m_document->page(pageNumber);
        if (!page)
            continue;

        const auto pageAnnotations = page->annotations();
        for (int annotationIndex = 0; annotationIndex < static_cast<int>(pageAnnotations.size()); ++annotationIndex)
        {
            const auto &annotation = pageAnnotations.at(annotationIndex);
            if (!annotation || annotation->subType() == Poppler::Annotation::AWidget || annotation->subType() == Poppler::Annotation::ALink)
                continue;

            const QString uniqueName = annotation->uniqueName().isEmpty()
                ? QStringLiteral("%1:%2").arg(pageNumber).arg(annotationIndex)
                : annotation->uniqueName();
            const auto style = annotation->style();
            result.append(QVariantMap{
                {QStringLiteral("page"), pageNumber},
                {QStringLiteral("index"), annotationIndex},
                {QStringLiteral("id"), uniqueName},
                {QStringLiteral("type"), annotationType(annotation->subType())},
                {QStringLiteral("rect"), annotation->boundary()},
                {QStringLiteral("contents"), annotation->contents()},
                {QStringLiteral("author"), annotation->author()},
                {QStringLiteral("color"), style.color()},
                {QStringLiteral("opacity"), style.opacity()}
            });
        }
    }

    return result;
}

bool PdfDocument::setFormFieldValue(int pageNumber, int fieldId, const QVariant &value)
{
    bool changed = false;
    {
        QMutexLocker popplerLocker(&popplerRenderMutex());
        if (!m_document || !m_document->okToFillForm() || pageNumber < 0 || pageNumber >= m_document->numPages())
            return false;

        const auto page = m_document->page(pageNumber);
        if (!page)
            return false;

        const auto fields = page->formFields();
        for (const auto &field : fields)
        {
            if (!field || field->id() != fieldId || field->isReadOnly())
                continue;

            if (const auto text = dynamic_cast<Poppler::FormFieldText *>(field.get()))
            {
                const QString newText = value.toString();
                if (text->text() != newText)
                {
                    text->setText(newText);
                    text->setAppearanceText(newText);
                    changed = true;
                }
            }
            else if (const auto choice = dynamic_cast<Poppler::FormFieldChoice *>(field.get()))
            {
                if (choice->isEditable() && value.userType() == QMetaType::QString)
                {
                    const QString newText = value.toString();
                    if (choice->editChoice() != newText)
                    {
                        choice->setEditChoice(newText);
#if POPPLER_VERSION_MAJOR > 24 || (POPPLER_VERSION_MAJOR == 24 && POPPLER_VERSION_MINOR >= 8)
                        choice->setAppearanceChoiceText(newText);
#endif
                        changed = true;
                    }
                }
                else
                {
                    QList<int> selected;
                    if (value.canConvert<QVariantList>())
                    {
                        for (const auto &entry : value.toList())
                            selected.append(entry.toInt());
                    }
                    else if (value.canConvert<int>())
                    {
                        selected.append(value.toInt());
                    }

                    if (selected != choice->currentChoices())
                    {
                        choice->setCurrentChoices(selected);
#if POPPLER_VERSION_MAJOR > 24 || (POPPLER_VERSION_MAJOR == 24 && POPPLER_VERSION_MINOR >= 8)
                        if (!selected.isEmpty())
                            choice->setAppearanceChoiceText(choice->choices().value(selected.first()));
#endif
                        changed = true;
                    }
                }
            }
            else if (const auto button = dynamic_cast<Poppler::FormFieldButton *>(field.get()))
            {
                const bool state = value.toBool();
                if (button->state() != state)
                {
                    button->setState(state);
                    changed = true;
                }
            }
            break;
        }
    }

    if (changed)
        markModified();
    return changed;
}

bool PdfDocument::setAnnotationProperties(int pageNumber, int annotationIndex, const QVariantMap &properties)
{
    bool changed = false;
    {
        QMutexLocker popplerLocker(&popplerRenderMutex());
        if (!m_document || !m_document->okToAddNotes() || pageNumber < 0 || pageNumber >= m_document->numPages())
            return false;

        const auto page = m_document->page(pageNumber);
        if (!page)
            return false;

        const auto pageAnnotations = page->annotations();
        if (annotationIndex < 0 || annotationIndex >= static_cast<int>(pageAnnotations.size()))
            return false;

        auto *annotation = pageAnnotations.at(annotationIndex).get();
        if (!annotation || annotation->subType() == Poppler::Annotation::AWidget || annotation->subType() == Poppler::Annotation::ALink)
            return false;

        if (properties.contains(QStringLiteral("contents")))
        {
            const QString contents = properties.value(QStringLiteral("contents")).toString();
            if (annotation->contents() != contents)
            {
                annotation->setContents(contents);
                changed = true;
            }
        }
        if (properties.contains(QStringLiteral("author")))
        {
            const QString author = properties.value(QStringLiteral("author")).toString();
            if (annotation->author() != author)
            {
                annotation->setAuthor(author);
                changed = true;
            }
        }
        if (properties.contains(QStringLiteral("rect")))
        {
            const QRectF boundary = properties.value(QStringLiteral("rect")).toRectF().normalized();
            if (annotation->boundary() != boundary)
            {
                annotation->setBoundary(boundary);
                changed = true;
            }
        }
        if (properties.contains(QStringLiteral("color")) || properties.contains(QStringLiteral("opacity")))
        {
            auto style = annotation->style();
            if (properties.contains(QStringLiteral("color")))
                style.setColor(properties.value(QStringLiteral("color")).value<QColor>());
            if (properties.contains(QStringLiteral("opacity")))
                style.setOpacity(properties.value(QStringLiteral("opacity")).toDouble());
            annotation->setStyle(style);
            changed = true;
        }
    }

    if (changed)
        markModified();
    return changed;
}

bool PdfDocument::addTextAnnotation(int pageNumber, const QRectF &rect, const QString &contents, const QString &author)
{
    {
        QMutexLocker popplerLocker(&popplerRenderMutex());
        if (!m_document || !m_document->okToAddNotes() || pageNumber < 0 || pageNumber >= m_document->numPages() || rect.isEmpty())
            return false;

        const auto page = m_document->page(pageNumber);
        if (!page)
            return false;

        auto *annotation = new Poppler::TextAnnotation(Poppler::TextAnnotation::InPlace);
        annotation->setBoundary(rect.normalized());
        annotation->setContents(contents);
        annotation->setAuthor(author);
        page->addAnnotation(annotation);
        delete annotation;
    }

    markModified();
    return true;
}

bool PdfDocument::removeAnnotation(int pageNumber, int annotationIndex)
{
    {
        QMutexLocker popplerLocker(&popplerRenderMutex());
        if (!m_document || !m_document->okToAddNotes() || pageNumber < 0 || pageNumber >= m_document->numPages())
            return false;

        const auto page = m_document->page(pageNumber);
        if (!page)
            return false;

        auto pageAnnotations = page->annotations();
        if (annotationIndex < 0 || annotationIndex >= static_cast<int>(pageAnnotations.size()))
            return false;

        auto *annotation = pageAnnotations.at(annotationIndex).get();
        if (!annotation || annotation->subType() == Poppler::Annotation::AWidget || annotation->subType() == Poppler::Annotation::ALink)
            return false;

        annotation = pageAnnotations.at(annotationIndex).release();
        page->removeAnnotation(annotation);
    }

    markModified();
    return true;
}

void PdfDocument::markModified()
{
    const bool wasModified = m_modified;
    m_modified = true;
    ++m_renderRevision;
    if (!wasModified)
        Q_EMIT modifiedChanged();
    Q_EMIT renderRevisionChanged();
    Q_EMIT formFieldsChanged();
    Q_EMIT annotationsChanged();

    if (m_providerState)
    {
        QMutexLocker locker(&m_providerState->mutex);
        m_providerState->tileCache.clear();
    }
}

bool PdfDocument::saveChanges()
{
    return saveToPath(m_path.toLocalFile());
}

bool PdfDocument::saveChangesAs(const QUrl &outputPath)
{
    if (outputPath.isEmpty() || !outputPath.isLocalFile())
        return false;
    return saveToPath(outputPath.toLocalFile());
}

bool PdfDocument::saveToPath(const QString &outputPath)
{
    if (outputPath.isEmpty())
        return false;

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly))
    {
        Q_EMIT error(QStringLiteral("Unable to open the PDF output file: %1").arg(outputPath));
        return false;
    }

    bool converted = false;
    {
        QMutexLocker popplerLocker(&popplerRenderMutex());
        if (!m_document)
            return false;

        auto converter = m_document->pdfConverter();
        if (!converter)
            return false;

        converter->setOutputDevice(&output);
        converter->setPDFOptions(converter->pdfOptions() | Poppler::PDFConverter::WithChanges);
        converted = converter->convert();
    }

    if (!converted || !output.commit())
    {
        Q_EMIT error(QStringLiteral("Unable to save the modified PDF: %1").arg(outputPath));
        return false;
    }

    if (m_modified)
    {
        m_modified = false;
        Q_EMIT modifiedChanged();
    }
    return true;
}

static QVariantMap convertDestination(const Poppler::LinkDestination& destination)
{
    QVariantMap result;
    result["page"] = destination.pageNumber() - 1;
    result["top"] = destination.top();
    result["left"] = destination.left();
    return result;
}

bool PdfDocument::loadPages()
{
    qDebug() << "Populating model...";
    m_pages.clear();

    if (!m_document)
        return false;

    QMutexLocker popplerLocker(&popplerRenderMutex());
    loadProvider();
    qDebug() << m_document->title() << m_document->numPages();

    QVariantList pageLinks;

    for(int i = 0; i < pages; i++)
    {
        auto page =  m_document->page(i);
        auto links = page->links();

        for (const auto& link : links)
        {
            if (link->linkType() == Poppler::Link::Goto)
            {
                auto gotoLink = static_cast<Poppler::LinkGoto*>(link.get());
                if (!gotoLink->isExternal())
                {
                    pageLinks.append(QVariantMap{{ "rect", link->linkArea().normalized() }, { "destination", convertDestination(gotoLink->destination()) }});
                }
            }
        }

        PdfItem item(QString("image://%1%2/page/%3").arg(m_id, QString::number(i % m_providersNumber), QString::number(i)), page->pageSize(), pageLinks);
        m_pages << item;
    }

    // Poppler::Document* document = m_document.get();

    // QtConcurrent::run( [=]
    // {
    //     PdfPagesList pages;

    //     for( int i = 0; i < document->numPages(); ++i )
    //     {
    //         std::unique_ptr<Poppler::Page> page = m_document->page(i);
    //         if (!page)
    //             continue;
    //         // remember the page
    //         pages.emplace_back(std::move(page));
    //     }

    //     // QMetaObject::invokeMethod(this, "_q_populate", Qt::QueuedConnection, Q_ARG(PdfPagesList, pages));
    // });

    return true;
}

// void PdfDocument::_q_populate(PdfPagesList pagesList)
// {
//     qDebug() << "Number of pages:" << pagesList.size();

//     // for (auto page : pagesList)
//     // {
//     //     if(page)
//     //         m_pages << page.get();
//     // }

//     qDebug() << "Model has been successfully populated!";
//     // Q_EMIT pagesLoaded();
// }

void PdfDocument::unlock(const QString &ownerPassword, const QString &password)
{
    if (! this->loadDocument(m_path.toLocalFile(), ownerPassword, password))
        return;


    loadPages();
}

QVariantList PdfDocument::search(int page, const QString &text, Qt::CaseSensitivity caseSensitivity)
{
    QVariantList result;
    QMutexLocker popplerLocker(&popplerRenderMutex());
    if (!m_document)
    {
        qWarning() << "Poppler plugin: no document to search";
        return result;
    }

    if (page >= m_document->numPages() || page < 0)
    {
        qWarning() << "Poppler plugin: search page" << page << "isn't in a document";
        return result;
    }

    auto p = m_document->page(page);
    if (!p)
        return result;

    auto searchResult = p->search(text, caseSensitivity == Qt::CaseInsensitive ? Poppler::Page::IgnoreCase : Poppler::Page::NoSearchFlags);

    auto pageSize = p->pageSizeF();
    for (const auto& r : searchResult)
    {
        result.append(QRectF(r.left() / pageSize.width(), r.top() / pageSize.height(), r.width() / pageSize.width(), r.height() / pageSize.height()));
    }
    return result;
}

QString PdfDocument::getText(const QRectF &rect, const QSize &pageSize, int page)
{
    QMutexLocker popplerLocker(&popplerRenderMutex());
    if (!m_document)
    {
        qWarning() << "Poppler plugin: no document to gather text";
        return QString();
    }

    if (page >= m_document->numPages() || page < 0)
    {
        qWarning() << "Poppler plugin: get text page" << page << "isn't in a document";
        return QString();
    }

    if (pageSize.width() <= 0 || pageSize.height() <= 0 || rect.width() <= 0 || rect.height() <= 0)
        return QString();


    auto p = m_document->page(page);
    if (!p)
        return QString();


    auto newRect = QRectF(rect.x() * p->pageSize().width()/pageSize.width(),
                          rect.y() * p->pageSize().height()/pageSize.height(), 
                          rect.width() * p->pageSize().width() / pageSize.width(),
                          rect.height() * p->pageSize().height() / pageSize.height());


     auto text = p->text(newRect);

     return text;
}

void PdfDocument::loadProvider()
{
    if (m_engine)
        return;

    // Poppler::Document is not safe for concurrent rendering. Keep requests
    // asynchronous while using one provider for this document.
    int newProvidersNumber = 1;

    if (newProvidersNumber != m_providersNumber) {
        m_providersNumber = newProvidersNumber;
        Q_EMIT providersNumberChanged();
    }

    qDebug() << "Ideal number of image providers is:" << m_providersNumber;

    qDebug() << "Loading image provider(s)...";
    QQmlContext *context = QQmlEngine::contextForObject(this);
    if (!context)
        return;

    QQmlEngine *engine = context->engine();
    if (!engine)
        return;

    m_engine = engine;

    for (int i=0; i<m_providersNumber; i++)
    {
        engine->addImageProvider(m_id+QByteArray::number(i), new PdfImageProvider(m_providerState));
    }

    qDebug() << "Image provider(s) loaded successfully !";
}

PdfDocument::~PdfDocument()
{
    if (m_providerState)
    {
        QMutexLocker locker(&m_providerState->mutex);
        m_providerState->document = nullptr;
        m_providerState->tileCache.clear();
    }

}
