#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QKeyEvent>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QLinearGradient>
#include <QFileSystemWatcher>

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

// ============================================================================
// CLEAN, SMOOTH, PURIFIED AUDIO WAVE CANVAS (ESTILO CAVA LIMPO)
// ============================================================================
class OndasVisualizerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OndasVisualizerWidget(QWidget *parent = nullptr)
        : QWidget(parent), m_barsCount(48), m_bands(48, 0.0), m_smoothBands(48, 0.0)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFocusPolicy(Qt::StrongFocus);

        // Load Wallpaper / Pywal Colors
        loadWallpaperTheme();

        // Watch pywal colors file
        m_walWatcher = new QFileSystemWatcher(this);
        QString walPath = QDir::homePath() + "/.cache/wal/colors";
        if (QFile::exists(walPath)) {
            m_walWatcher->addPath(walPath);
            connect(m_walWatcher, &QFileSystemWatcher::fileChanged, this, &OndasVisualizerWidget::loadWallpaperTheme);
        }

        // Animation Timer (~60 FPS)
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &OndasVisualizerWidget::updateFrame);
        m_timer->start(16);

        // Launch CAVA System Audio listener
        startCavaProcess();
    }

    ~OndasVisualizerWidget() override
    {
        stopCavaProcess();
    }

public slots:
    void loadWallpaperTheme()
    {
        QString walColorsPath = QDir::homePath() + "/.cache/wal/colors";
        QFile file(walColorsPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QStringList lines;
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (!line.isEmpty()) lines.append(line);
            }
            file.close();

            if (lines.size() >= 7) {
                m_primaryColor   = makeVibrant(QColor(lines[1])); // Neon Main
                m_secondaryColor = makeVibrant(QColor(lines[2])); // Neon Secondary
                m_glowColor      = makeVibrant(QColor(lines[3])); // Gold Accent
                return;
            }
        }

        // Fallback Palette
        m_primaryColor   = QColor("#00f0ff"); // Electric Cyan
        m_secondaryColor = QColor("#ff007f"); // Hot Neon Pink
        m_glowColor      = QColor("#ffe600"); // Yellow Gold
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Q || event->key() == Qt::Key_Escape) {
            qApp->quit();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // 100% PURE TRANSPARENT CANVAS (NO BG, NO BOX, NO OVERLAY)
        painter.fillRect(rect(), Qt::transparent);

        // Draw Smooth, Clean Audio Waves
        drawAudioWaves(painter);
    }

private slots:
    void readCavaOutput()
    {
        if (!m_cavaProcess) return;

        m_cavaBuffer.append(m_cavaProcess->readAllStandardOutput());
        int lastNewline = m_cavaBuffer.lastIndexOf('\n');
        if (lastNewline != -1) {
            QByteArray latestData = m_cavaBuffer.mid(0, lastNewline);
            m_cavaBuffer = m_cavaBuffer.mid(lastNewline + 1);

            QStringList lines = QString::fromUtf8(latestData).split('\n', Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                QString lastLine = lines.last().trimmed();
                QStringList values = lastLine.split(';', Qt::SkipEmptyParts);

                int count = std::min(static_cast<int>(values.size()), m_barsCount);
                for (int i = 0; i < count; ++i) {
                    double val = values[i].toDouble() / 100.0;
                    m_bands[i] = std::clamp(val, 0.0, 1.0);
                }
            }
        }
    }

    void updateFrame()
    {
        // Smooth CAVA Monstercat-style exponential decay
        for (int i = 0; i < m_barsCount; ++i) {
            if (m_bands[i] > m_smoothBands[i]) {
                m_smoothBands[i] = m_smoothBands[i] * 0.5 + m_bands[i] * 0.5;
            } else {
                m_smoothBands[i] = m_smoothBands[i] * 0.88 + m_bands[i] * 0.12;
            }
        }

        m_animTime += 0.02;
        update();
    }

private:
    QColor makeVibrant(QColor col)
    {
        int h, s, v, a;
        col.getHsv(&h, &s, &v, &a);
        s = std::min(255, static_cast<int>(s * 1.4 + 50));
        v = std::min(255, static_cast<int>(v * 1.3 + 40));
        return QColor::fromHsv(h < 0 ? 0 : h, s, v, a);
    }

    void startCavaProcess()
    {
        m_cavaConfigFile = QDir::tempPath() + "/aether_cava_clean_waves.conf";
        QFile file(m_cavaConfigFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "[general]\n"
                << "bars = 48\n"
                << "framerate = 60\n"
                << "sensitivity = 180\n"
                << "autosens = 1\n"
                << "[smoothing]\n"
                << "integral = 0\n"
                << "monstercat = 1\n"
                << "gravity = 100\n"
                << "[output]\n"
                << "method = raw\n"
                << "raw_target = /dev/stdout\n"
                << "data_format = ascii\n"
                << "ascii_max_range = 100\n"
                << "bar_delimiter = 59\n";
            file.close();

            m_cavaProcess = new QProcess(this);
            connect(m_cavaProcess, &QProcess::readyReadStandardOutput, this, &OndasVisualizerWidget::readCavaOutput);
            m_cavaProcess->start("cava", QStringList() << "-p" << m_cavaConfigFile);
        }
    }

    void stopCavaProcess()
    {
        if (m_cavaProcess) {
            if (m_cavaProcess->state() != QProcess::NotRunning) {
                m_cavaProcess->kill();
                m_cavaProcess->waitForFinished(300);
            }
            delete m_cavaProcess;
            m_cavaProcess = nullptr;
        }
        if (!m_cavaConfigFile.isEmpty()) {
            QFile::remove(m_cavaConfigFile);
        }
    }

    void drawAudioWaves(QPainter &painter)
    {
        int w = width();
        int h = height();
        if (w <= 0 || h <= 0) return;

        double centerY = h * 0.50;
        double maxWaveHeight = h * 0.35;

        std::vector<QPointF> ptsMain;
        std::vector<QPointF> ptsDashed;

        int numPoints = 75;
        ptsMain.reserve(numPoints + 1);
        ptsDashed.reserve(numPoints + 1);

        for (int i = 0; i <= numPoints; ++i) {
            double normX = i / static_cast<double>(numPoints);
            double x = normX * w;

            double distFromCenter = std::abs(normX - 0.5) * 2.0;
            int bandIdx;
            if (distFromCenter < 0.35) {
                bandIdx = static_cast<int>((distFromCenter / 0.35) * 8.0);
            } else {
                bandIdx = std::clamp(static_cast<int>(8.0 + ((distFromCenter - 0.35) / 0.65) * 40.0), 0, m_barsCount - 1);
            }

            double bandVal = m_smoothBands[bandIdx];
            double amp = (0.04 + bandVal * 0.96) * maxWaveHeight;

            double wavePhase1 = std::sin(normX * M_PI * 5.0 + m_animTime * 2.5);
            double wavePhase2 = std::cos(normX * M_PI * 7.0 - m_animTime * 2.0);

            double yMain = centerY - (amp * (0.65 + wavePhase1 * 0.35));
            double yDashed = centerY + (amp * (0.55 + wavePhase2 * 0.30));

            ptsMain.push_back(QPointF(x, yMain));
            ptsDashed.push_back(QPointF(x, yDashed));
        }

        QPainterPath pathMain = createSmoothSpline(ptsMain);
        QPainterPath pathDashed = createSmoothSpline(ptsDashed);

        // 1. Main Solid Wave (Hot Neon Pink -> Cyan -> Gold)
        QLinearGradient mainGrad(0, 0, w, 0);
        mainGrad.setColorAt(0.0, m_secondaryColor);
        mainGrad.setColorAt(0.4, m_primaryColor);
        mainGrad.setColorAt(0.7, m_glowColor);
        mainGrad.setColorAt(1.0, m_secondaryColor);

        // Soft Ambient Bloom Halo
        QPen glowPen(QBrush(mainGrad), 10.0);
        glowPen.setCapStyle(Qt::RoundCap);
        glowPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(glowPen);
        painter.setOpacity(0.35);
        painter.drawPath(pathMain);

        // Crisp Core Wave Line
        QPen corePen(QBrush(mainGrad), 3.5);
        corePen.setCapStyle(Qt::RoundCap);
        corePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(corePen);
        painter.setOpacity(1.0);
        painter.drawPath(pathMain);

        // 2. Secondary Dashed Echo Ribbon
        QLinearGradient dashGrad(0, 0, w, 0);
        dashGrad.setColorAt(0.0, m_glowColor);
        dashGrad.setColorAt(0.5, m_primaryColor);
        dashGrad.setColorAt(1.0, m_glowColor);

        QPen dashPen(QBrush(dashGrad), 2.8, Qt::DashLine);
        dashPen.setCapStyle(Qt::RoundCap);
        dashPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(dashPen);
        painter.setOpacity(0.85);
        painter.drawPath(pathDashed);
    }

    QPainterPath createSmoothSpline(const std::vector<QPointF> &pts)
    {
        QPainterPath path;
        if (pts.empty()) return path;

        path.moveTo(pts[0]);
        for (size_t i = 0; i < pts.size() - 1; ++i) {
            QPointF p0 = (i == 0) ? pts[i] : pts[i - 1];
            QPointF p1 = pts[i];
            QPointF p2 = pts[i + 1];
            QPointF p3 = (i + 2 < pts.size()) ? pts[i + 2] : p2;

            double ctrl1X = p1.x() + (p2.x() - p0.x()) / 6.0;
            double ctrl1Y = p1.y() + (p2.y() - p0.y()) / 6.0;
            double ctrl2X = p2.x() - (p3.x() - p1.x()) / 6.0;
            double ctrl2Y = p2.y() - (p3.y() - p1.y()) / 6.0;

            path.cubicTo(ctrl1X, ctrl1Y, ctrl2X, ctrl2Y, p2.x(), p2.y());
        }
        return path;
    }

private:
    QByteArray m_cavaBuffer;
    QTimer *m_timer = nullptr;
    QFileSystemWatcher *m_walWatcher = nullptr;
    double m_animTime = 0.0;

    QProcess *m_cavaProcess = nullptr;
    QString m_cavaConfigFile;

    int m_barsCount;
    std::vector<double> m_bands;
    std::vector<double> m_smoothBands;

    QColor m_primaryColor;
    QColor m_secondaryColor;
    QColor m_glowColor;
};

// ============================================================================
// MAIN APPLICATION WINDOW
// ============================================================================
class OndasMainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OndasMainWindow(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowTitle("Ondas Audio Visualizer");
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        resize(950, 480);

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_visualizer = new OndasVisualizerWidget(this);
        layout->addWidget(m_visualizer);
    }

private:
    OndasVisualizerWidget *m_visualizer = nullptr;
};

#include "main.moc"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    std::cout << "\n🌊 Ondas Visualizer (Limpo, Suave & Transparente - Estilo CAVA)" << std::endl;
    std::cout << "🎨 Cores sincronizadas com o Wallpaper (Pywal)." << std::endl;
    std::cout << "💡 Pressione 'Q', 'Esc' ou Ctrl+C para encerrar.\n" << std::endl;

    OndasMainWindow window;
    window.show();

    return app.exec();
}
