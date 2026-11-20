#include "spectrogram3d.h"
#include "spectrogram.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

#include <qwt_color_map.h>
#include <qwt_interval.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

constexpr float surfaceHeight = 1.3f;
constexpr float minimumDistance = 2.8f;
constexpr float maximumDistance = 9.0f;

struct Vertex
{
    float x, y, z;
    float nx, ny, nz;
};

QString tr3d(const char *source)
{
    return Spectrogram3DView::tr(source);
}

} // namespace

class SurfaceCanvas final : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    explicit SurfaceCanvas(const SpectrogramView &history, QWidget *parent = nullptr)
        : QOpenGLWidget(parent), history(history),
          vertices(QOpenGLBuffer::VertexBuffer), indices(QOpenGLBuffer::IndexBuffer),
          gridVertices(QOpenGLBuffer::VertexBuffer)
    {
        setObjectName(QStringLiteral("spectrogram3DCanvas"));
        setMinimumSize(250, 210);
        auto requested = format();
        requested.setDepthBufferSize(24);
        requested.setSamples(4);
        setFormat(requested);
        setFocusPolicy(Qt::StrongFocus);
    }

    ~SurfaceCanvas() override
    {
        cleanup();
    }

    void updateHistory()
    {
        meshDirty = true;
        update();
    }

    void setAppearance(const QPalette &palette, const QColor &color)
    {
        setPalette(palette);
        background = color;
        update();
    }

    void resetCamera()
    {
        azimuth = -35.0f;
        elevation = 31.0f;
        distance = 5.4f;
        update();
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { cleanup(); },
                Qt::DirectConnection);
        program = std::make_unique<QOpenGLShaderProgram>();
        const bool vertexCompiled = program->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
            attribute vec3 position;
            attribute vec3 normal;
            uniform mat4 mvp;
            varying vec3 surfaceNormal;
            varying float level;
            void main()
            {
                gl_Position = mvp * vec4(position, 1.0);
                surfaceNormal = normal;
                level = position.y / 1.3;
            }
        )");
        const bool fragmentCompiled = program->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
            uniform sampler2D levelColors;
            uniform bool solid;
            uniform vec4 solidColor;
            varying vec3 surfaceNormal;
            varying float level;
            void main()
            {
                if (solid) {
                    gl_FragColor = solidColor;
                } else {
                    vec3 color = texture2D(levelColors, vec2(clamp(level, 0.0, 1.0), 0.5)).rgb;
                    vec3 light = normalize(vec3(-0.4, 1.0, 0.6));
                    float shade = 0.68 + 0.32 * abs(dot(normalize(surfaceNormal), light));
                    gl_FragColor = vec4(color * shade, 1.0);
                }
            }
        )");
        if (!vertexCompiled || !fragmentCompiled || !program->link()) {
            qWarning("TR4131 3D shader: %s", qPrintable(program->log()));
            program.reset();
            return;
        }
        positionAttribute = program->attributeLocation("position");
        normalAttribute = program->attributeLocation("normal");
        vertices.create();
        vertices.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        indices.create();
        indices.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        gridVertices.create();

        std::vector<Vertex> lines;
        const auto addLine = [&lines](QVector3D start, QVector3D end) {
            lines.push_back({start.x(), start.y(), start.z(), 0, 1, 0});
            lines.push_back({end.x(), end.y(), end.z(), 0, 1, 0});
        };
        for (int tick = 0; tick <= 4; ++tick) {
            const float coordinate = -1.0f + tick * 0.5f;
            addLine({coordinate, 0, -1}, {coordinate, 0, 1});
            addLine({-1, 0, coordinate}, {1, 0, coordinate});
            const float y = tick * surfaceHeight / 4.0f;
            addLine({-1, y, -1}, {1, y, -1});
        }
        addLine({-1, 0, -1}, {-1, surfaceHeight, -1});
        addLine({1, 0, -1}, {1, surfaceHeight, -1});
        gridVertices.bind();
        gridVertices.allocate(lines.data(), static_cast<int>(lines.size() * sizeof(Vertex)));
        gridVertices.release();
        gridVertexCount = static_cast<int>(lines.size());

        QImage colors(256, 1, QImage::Format_RGBA8888);
        std::unique_ptr<QwtLinearColorMap> colorMap(spectrogramColorMap());
        for (int index = 0; index < colors.width(); ++index) {
            colors.setPixelColor(index, 0, QColor::fromRgb(colorMap->rgb(QwtInterval(0, 1), index / 255.0)));
        }
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, colors.width(), 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.constBits());
        glBindTexture(GL_TEXTURE_2D, 0);
        meshDirty = true;
    }

    void paintGL() override
    {
        glViewport(0, 0, qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
        glClearColor(background.redF(), background.greenF(), background.blueF(), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!program) {
            QPainter painter(this);
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(rect().adjusted(30, 30, -30, -30), Qt::AlignCenter | Qt::TextWordWrap,
                             tr3d("3D-Darstellung konnte nicht initialisiert werden."));
            return;
        }
        if (meshDirty) {
            rebuildMesh();
        }

        constexpr float radians = 3.14159265358979323846f / 180.0f;
        const QVector3D eye(distance * std::sin(azimuth * radians) * std::cos(elevation * radians),
                            0.45f + distance * std::sin(elevation * radians),
                            distance * std::cos(azimuth * radians) * std::cos(elevation * radians));
        QMatrix4x4 view;
        view.lookAt(eye, QVector3D(0, 0.45f, 0), QVector3D(0, 1, 0));
        QMatrix4x4 projection;
        projection.perspective(35.0f, float(width()) / std::max(1, height()), 0.1f, 30.0f);
        const QMatrix4x4 mvp = projection * view;

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        program->bind();
        program->setUniformValue("mvp", mvp);
        program->setUniformValue("solid", true);
        const QColor lineColor = palette().color(QPalette::Mid);
        program->setUniformValue("solidColor", QVector4D(lineColor.redF(), lineColor.greenF(), lineColor.blueF(), 1.0f));
        bindAttributes(gridVertices);
        glDrawArrays(GL_LINES, 0, gridVertexCount);
        releaseAttributes(gridVertices);

        if (indexCount || singleRowCount) {
            program->setUniformValue("solid", false);
            program->setUniformValue("levelColors", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, colorTexture);
            bindAttributes(vertices);
            if (indexCount) {
                indices.bind();
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, 1.0f);
                glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
                glDisable(GL_POLYGON_OFFSET_FILL);
                indices.release();
            } else {
                glDrawArrays(GL_LINE_STRIP, 0, singleRowCount);
            }
            releaseAttributes(vertices);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        program->release();
        glDisable(GL_DEPTH_TEST);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        drawAxes(painter, mvp);
        if (!history.hasHistory()) {
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(rect().adjusted(25, 20, -25, -20), Qt::AlignTop | Qt::AlignHCenter,
                             tr3d("Messung abrufen, um die 3D-Ansicht zu starten."));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            lastMouse = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
        } else {
            QOpenGLWidget::mousePressEvent(event);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            const auto delta = event->position() - lastMouse;
            lastMouse = event->position();
            azimuth = std::remainder(azimuth + float(delta.x() * 0.45), 360.0f);
            elevation = std::clamp(elevation + float(delta.y() * 0.35), 8.0f, 80.0f);
            update();
            event->accept();
        } else {
            QOpenGLWidget::mouseMoveEvent(event);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        unsetCursor();
        QOpenGLWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        distance = std::clamp(distance * std::exp(-event->angleDelta().y() / 1000.0f),
                              minimumDistance, maximumDistance);
        update();
        event->accept();
    }

private:
    void cleanup()
    {
        if (!context() || !context()->isValid()) {
            return;
        }
        disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, nullptr);
        makeCurrent();
        vertices.destroy();
        indices.destroy();
        gridVertices.destroy();
        if (colorTexture) {
            glDeleteTextures(1, &colorTexture);
            colorTexture = 0;
        }
        program.reset();
        doneCurrent();
    }

    void bindAttributes(QOpenGLBuffer &buffer)
    {
        buffer.bind();
        program->enableAttributeArray(positionAttribute);
        program->setAttributeBuffer(positionAttribute, GL_FLOAT, offsetof(Vertex, x), 3, sizeof(Vertex));
        program->enableAttributeArray(normalAttribute);
        program->setAttributeBuffer(normalAttribute, GL_FLOAT, offsetof(Vertex, nx), 3, sizeof(Vertex));
    }

    void releaseAttributes(QOpenGLBuffer &buffer)
    {
        program->disableAttributeArray(positionAttribute);
        program->disableAttributeArray(normalAttribute);
        buffer.release();
    }

    void rebuildMesh()
    {
        meshDirty = false;
        indexCount = 0;
        singleRowCount = 0;
        const auto snapshot = history.snapshot();
        if (snapshot.sweeps.empty()) {
            return;
        }

        const auto columns = static_cast<int>(snapshot.sweeps.back().size());
        const auto rows = static_cast<int>(snapshot.sweeps.size());
        const float dx = 2.0f / (columns - 1);
        const float dz = 2.0f / std::max(1, snapshot.depth - 1);
        const auto heightAt = [&snapshot](int row, int column) {
            const auto &values = snapshot.sweeps[snapshot.sweeps.size() - 1 - row];
            const double level = (values[column] - snapshot.levelMin) / (snapshot.levelMax - snapshot.levelMin);
            return static_cast<float>(std::clamp(level, 0.0, 1.0)) * surfaceHeight;
        };

        std::vector<Vertex> mesh;
        mesh.reserve(static_cast<std::size_t>(rows) * columns);
        for (int row = 0; row < rows; ++row) {
            const int previousRow = std::max(0, row - 1);
            const int nextRow = std::min(rows - 1, row + 1);
            for (int column = 0; column < columns; ++column) {
                const int previousColumn = std::max(0, column - 1);
                const int nextColumn = std::min(columns - 1, column + 1);
                const float gradientX = (heightAt(row, nextColumn) - heightAt(row, previousColumn))
                    / ((nextColumn - previousColumn) * dx);
                const float gradientZ = rows == 1 ? 0.0f
                    : (heightAt(previousRow, column) - heightAt(nextRow, column))
                        / ((nextRow - previousRow) * dz);
                mesh.push_back({-1.0f + column * dx, heightAt(row, column), 1.0f - row * dz,
                                -gradientX, 1.0f, -gradientZ});
            }
        }
        vertices.bind();
        vertices.allocate(mesh.data(), static_cast<int>(mesh.size() * sizeof(Vertex)));
        vertices.release();
        if (rows == 1) {
            singleRowCount = columns;
            return;
        }

        std::vector<GLuint> triangles;
        triangles.reserve(static_cast<std::size_t>(rows - 1) * (columns - 1) * 6);
        for (int row = 0; row < rows - 1; ++row) {
            for (int column = 0; column < columns - 1; ++column) {
                const GLuint first = row * columns + column;
                const GLuint next = first + columns;
                triangles.insert(triangles.end(), {first, next, first + 1, first + 1, next, next + 1});
            }
        }
        indices.bind();
        indices.allocate(triangles.data(), static_cast<int>(triangles.size() * sizeof(GLuint)));
        indices.release();
        indexCount = static_cast<int>(triangles.size());
    }

    void drawAxes(QPainter &painter, const QMatrix4x4 &mvp)
    {
        const auto snapshot = history.snapshot();
        const auto projected = [this, &mvp](QVector3D point) {
            const QVector4D clip = mvp * QVector4D(point, 1);
            return QPointF((clip.x() / clip.w() + 1.0f) * width() / 2.0,
                           (1.0f - clip.y() / clip.w()) * height() / 2.0);
        };
        const auto caption = [&painter, &projected](QVector3D point, const QString &text, bool bold = false) {
            QFont font = painter.font();
            font.setBold(bold);
            painter.setFont(font);
            const auto position = projected(point);
            const auto bounds = painter.fontMetrics().boundingRect(text).adjusted(-4, -2, 4, 2);
            const QRectF centered(position.x() - bounds.width() / 2.0, position.y() - bounds.height() / 2.0,
                                  bounds.width(), bounds.height());
            painter.drawText(centered, Qt::AlignCenter, text);
        };
        painter.setPen(palette().color(QPalette::Text));
        const auto format = locale();
        for (int tick = 0; tick <= 4; ++tick) {
            const float fraction = tick / 4.0f;
            caption({-1 + 2 * fraction, -0.09f, 1.09f},
                    format.toString(snapshot.start + fraction * (snapshot.stop - snapshot.start), 'g', 5));
            caption({-1.18f, tick == 4 ? -0.18f : -0.03f, 1 - 2 * fraction},
                    QString::number(qRound(fraction * (snapshot.depth - 1))));
            caption({-1.12f, fraction * surfaceHeight, -1.06f},
                    format.toString(snapshot.levelMin + fraction * (snapshot.levelMax - snapshot.levelMin), 'g', 4));
        }
        caption({0, -0.28f, 1.23f},
                snapshot.frequency ? tr3d("Frequenz [MHz]") : tr3d("Zeit [ms]"), true);
        caption({-1.56f, -0.13f, 0}, tr3d("Messungen zurück"), true);
        caption({-1.08f, surfaceHeight + 0.16f, -1.05f}, tr3d("Pegel [%1]").arg(snapshot.unit), true);
    }

    const SpectrogramView &history;
    QOpenGLBuffer vertices;
    QOpenGLBuffer indices;
    QOpenGLBuffer gridVertices;
    std::unique_ptr<QOpenGLShaderProgram> program;
    GLuint colorTexture = 0;
    int positionAttribute = -1;
    int normalAttribute = -1;
    int indexCount = 0;
    int singleRowCount = 0;
    int gridVertexCount = 0;
    bool meshDirty = true;
    QColor background = QColor("#101925");
    QPointF lastMouse;
    float azimuth = -35.0f;
    float elevation = 31.0f;
    float distance = 5.4f;
};

Spectrogram3DView::Spectrogram3DView(SpectrogramView &history, QWidget *parent)
    : QFrame(parent), history(history), depthLabel(new QLabel), summary(new QLabel),
      instructions(new QLabel), depthEditor(new QSpinBox), clearButton(new QPushButton),
      resetButton(new QPushButton), canvas(new SurfaceCanvas(history))
{
    setObjectName(QStringLiteral("plotPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(9);
    auto *toolbar = new QHBoxLayout;
    auto *source = new QLabel(QStringLiteral("WRITE"));
    source->setObjectName(QStringLiteral("spectrogramSource"));
    toolbar->addWidget(source);
    toolbar->addStretch();
    depthLabel->setBuddy(depthEditor);
    toolbar->addWidget(depthLabel);
    depthEditor->setObjectName(QStringLiteral("spectrogram3DDepth"));
    depthEditor->setRange(1, 1000);
    depthEditor->setKeyboardTracking(false);
    toolbar->addWidget(depthEditor);
    clearButton->setObjectName(QStringLiteral("clearSpectrogram3D"));
    toolbar->addWidget(clearButton);
    layout->addLayout(toolbar);
    layout->addWidget(canvas, 1);
    auto *footer = new QHBoxLayout;
    auto *notes = new QVBoxLayout;
    notes->setSpacing(3);
    summary->setObjectName(QStringLiteral("quiet"));
    instructions->setObjectName(QStringLiteral("quiet"));
    summary->setWordWrap(true);
    instructions->setWordWrap(true);
    notes->addWidget(summary);
    notes->addWidget(instructions);
    footer->addLayout(notes, 1);
    resetButton->setObjectName(QStringLiteral("resetSpectrogram3D"));
    footer->addWidget(resetButton, 0, Qt::AlignVCenter);
    layout->addLayout(footer);

    connect(&history, &SpectrogramView::historyChanged, this, &Spectrogram3DView::updateHistory);
    connect(depthEditor, &QSpinBox::valueChanged, &history, &SpectrogramView::setDepth);
    connect(clearButton, &QPushButton::clicked, &history, &SpectrogramView::clearHistory);
    connect(resetButton, &QPushButton::clicked, this, [this] { canvas->resetCamera(); });
    retranslate();
}

void Spectrogram3DView::setAppearance(const QPalette &palette, const QColor &canvasColor)
{
    canvas->setAppearance(palette, canvasColor);
}

void Spectrogram3DView::retranslate()
{
    depthLabel->setText(tr("Verlaufstiefe"));
    depthEditor->setToolTip(tr("Anzahl der Messungen im Spektrogramm"));
    clearButton->setText(tr("Verlauf leeren"));
    resetButton->setText(tr("Ansicht zurücksetzen"));
    instructions->setText(tr("Ziehen zum Drehen · Mausrad zum Zoomen"));
    updateHistory();
}

void Spectrogram3DView::updateHistory()
{
    const auto snapshot = history.snapshot();
    if (depthEditor->value() != snapshot.depth) {
        const QSignalBlocker block(depthEditor);
        depthEditor->setValue(snapshot.depth);
    }
    clearButton->setEnabled(!snapshot.sweeps.empty());
    summary->setText(tr("%1 / %2 Messungen · Neueste vorne").arg(snapshot.sweeps.size()).arg(snapshot.depth));
    canvas->updateHistory();
}

QPixmap Spectrogram3DView::grabPlot()
{
    return QPixmap::fromImage(canvas->grabFramebuffer());
}

int Spectrogram3DView::commitDepth()
{
    depthEditor->interpretText();
    return history.commitDepth();
}
