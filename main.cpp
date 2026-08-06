#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QFileSystemWatcher>

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

// ============================================================================
// STRUCTS
// ============================================================================
struct Shockwave {
    double x, y;
    double radius;
    double maxRadius;
    double alpha;
    double strokeWidth;
    QColor color;
};

struct Sparkle {
    double x, y;
    double vx, vy;
    double size;
    double alpha;
    double decay;
    QColor color;
};

// ============================================================================
// PURE FRAMELESS 100% TRANSPARENT AUDIO WAVE CANVAS
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

        // Watch pywal colors file for real-time wallpaper color changes
        m_walWatcher = new QFileSystemWatcher(this);
        QString walPath = QDir::homePath() + "/.cache/wal/colors";
        if (QFile::exists(walPath)) {
            m_walWatcher->addPath(walPath);
            connect(m_walWatcher, &QFileSystemWatcher::fileChanged, this, &OndasVisualizerWidget::loadWallpaperTheme);
        }

        // Animation Loop (~60 FPS)
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &OndasVisualizerWidget::updateFrame);
        m_timer->start(16);

        // Launch CAVA System Audio Process
        startCavaProcess();
    }

    ~OndasVisualizerWidget() override
    {
        stopCavaProcess();
    }

public slots:
    void loadWallpaperTheme()
    {
        // 1. Try Pywal wallpaper colors (~/.cache/wal/colors)
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
                m_primaryColor   = makeVibrant(QColor(lines[1])); // Neon Main Accent
                m_secondaryColor = makeVibrant(QColor(lines[2])); // Neon Secondary
                m_glowColor      = makeVibrant(QColor(lines[3])); // Gold / Glow
                m_accentColor    = makeVibrant(QColor(lines[5])); // Sparkle Highlight
                return;
            }
        }

        // 2. Fallback Ultra-Vibrant Palette
        m_primaryColor   = QColor("#00f0ff"); // Electric Cyan
        m_secondaryColor = QColor("#ff007f"); // Hot Neon Pink
        m_glowColor      = QColor("#ffe600"); // Yellow Gold
        m_accentColor    = QColor("#d946ef"); // Neon Violet
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

    // Allow clicking and dragging the frameless window anywhere
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            move(event->globalPosition().toPoint() - m_dragPosition);
            event->accept();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // 100% TRANSPARENT CANVAS (NO DARK BACKPLATE / NO OVERLAY RECTANGLE)
        painter.fillRect(rect(), Qt::transparent);

        painter.save();
        painter.translate(m_shakeX, m_shakeY);

        // 1. Draw Multi-Layer Glowing Audio Waveforms
        drawAudioWaves(painter);

        // 2. Draw Explosive Bass Shockwaves
        drawShockwaves(painter);

        // 3. Draw Sparkles
        drawSparkles(painter);

        painter.restore();
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
        // Fast Attack & Smooth Decay Frequencies
        m_bassEnergy = 0.0;
        m_midEnergy = 0.0;
        m_trebleEnergy = 0.0;

        for (int i = 0; i < m_barsCount; ++i) {
            if (m_bands[i] > m_smoothBands[i]) {
                m_smoothBands[i] = m_bands[i]; // Instant attack
            } else {
                m_smoothBands[i] = m_smoothBands[i] * 0.80 + m_bands[i] * 0.20;
            }

            if (i < 8) m_bassEnergy += m_smoothBands[i];
            else if (i < 30) m_midEnergy += m_smoothBands[i];
            else m_trebleEnergy += m_smoothBands[i];
        }

        m_bassEnergy /= 8.0;
        m_midEnergy /= 22.0;
        m_trebleEnergy /= 18.0;

        // Automatic Bass Peak Burst Detection
        if (m_bassCooldown > 0) m_bassCooldown--;

        if (m_bassEnergy > 0.38 && m_bassCooldown == 0) {
            auto *rng = QRandomGenerator::global();
            double burstX = width() * (0.3 + rng->generateDouble() * 0.4);
            double burstY = height() * 0.5;
            triggerExplosiveBurst(burstX, burstY, m_bassEnergy);
            m_bassCooldown = 12;
        }

        // Screen Shake Physics on Heavy Bass
        double shakeMagnitude = std::pow(m_bassEnergy, 2.0) * 32.0;
        auto *rng = QRandomGenerator::global();
        m_shakeX = (rng->generateDouble() * 2.0 - 1.0) * shakeMagnitude;
        m_shakeY = (rng->generateDouble() * 2.0 - 1.0) * shakeMagnitude;

        // High frequency sparkles
        if (m_trebleEnergy > 0.3 && rng->generateDouble() > 0.4) {
            spawnSparkle(rng->generateDouble() * width(), rng->generateDouble() * height(), 2);
        }

        updateShockwaves();
        updateSparkles();

        m_animTime += 0.03;
        update();
    }

private:
    QColor makeVibrant(QColor col)
    {
        int h, s, v, a;
        col.getHsv(&h, &s, &v, &a);
        s = std::min(255, static_cast<int>(s * 1.5 + 60));
        v = std::min(255, static_cast<int>(v * 1.4 + 50));
        return QColor::fromHsv(h < 0 ? 0 : h, s, v, a);
    }

    void startCavaProcess()
    {
        m_cavaConfigFile = QDir::tempPath() + "/aether_cava_pure_waves.conf";
        QFile file(m_cavaConfigFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "[general]\n"
                << "bars = 48\n"
                << "framerate = 60\n"
                << "sensitivity = 200\n"
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

    void triggerExplosiveBurst(double x, double y, double intensity)
    {
        Shockwave sw1;
        sw1.x = x;
        sw1.y = y;
        sw1.radius = 10.0;
        sw1.maxRadius = 240.0 + intensity * 160.0;
        sw1.alpha = 1.0;
        sw1.strokeWidth = 4.0;
        sw1.color = m_secondaryColor;
        m_shockwaves.push_back(sw1);

        Shockwave sw2;
        sw2.x = x;
        sw2.y = y;
        sw2.radius = 5.0;
        sw2.maxRadius = 170.0 + intensity * 110.0;
        sw2.alpha = 0.95;
        sw2.strokeWidth = 2.5;
        sw2.color = m_glowColor;
        m_shockwaves.push_back(sw2);

        spawnSparkle(x, y, static_cast<int>(30 + intensity * 30));
    }

    void updateShockwaves()
    {
        for (auto it = m_shockwaves.begin(); it != m_shockwaves.end();) {
            it->radius += 8.0 + m_bassEnergy * 12.0;
            it->alpha -= 0.024;
            if (it->alpha <= 0.0 || it->radius >= it->maxRadius) {
                it = m_shockwaves.erase(it);
            } else {
                ++it;
            }
        }
    }

    void spawnSparkle(double x, double y, int count)
    {
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < count; ++i) {
            Sparkle s;
            s.x = x;
            s.y = y;
            double angle = rng->generateDouble() * M_PI * 2.0;
            double speed = 1.5 + rng->generateDouble() * (6.0 + m_trebleEnergy * 8.0);
            s.vx = std::cos(angle) * speed;
            s.vy = std::sin(angle) * speed;
            s.size = 2.0 + rng->generateDouble() * 4.5;
            s.alpha = 1.0;
            s.decay = 0.02 + rng->generateDouble() * 0.04;
            s.color = (i % 3 == 0) ? m_glowColor : (i % 3 == 1 ? m_secondaryColor : m_primaryColor);
            m_sparkles.push_back(s);
        }
    }

    void updateSparkles()
    {
        for (auto it = m_sparkles.begin(); it != m_sparkles.end();) {
            it->x += it->vx;
            it->y += it->vy;
            it->alpha -= it->decay;
            if (it->alpha <= 0.0) {
                it = m_sparkles.erase(it);
            } else {
                ++it;
            }
        }
    }

    void drawAudioWaves(QPainter &painter)
    {
        int w = width();
        int h = height();
        if (w <= 0 || h <= 0) return;

        double centerY = h * 0.50;
        double maxWaveHeight = h * 0.40;

        std::vector<QPointF> ptsMain;
        std::vector<QPointF> ptsDashed;

        int numPoints = 80;
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
            double amp = (0.05 + bandVal * 0.95) * maxWaveHeight;

            double wavePhase1 = std::sin(normX * M_PI * 6.0 + m_animTime * 3.5);
            double wavePhase2 = std::cos(normX * M_PI * 8.0 - m_animTime * 2.8);

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

        // Outer Wide Ambient Bloom Halo
        QPen glowPen(QBrush(mainGrad), 12.0 + m_bassEnergy * 10.0);
        glowPen.setCapStyle(Qt::RoundCap);
        glowPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(glowPen);
        painter.setOpacity(0.38 + m_bassEnergy * 0.35);
        painter.drawPath(pathMain);

        // Bright Core Stroke
        QPen corePen(QBrush(mainGrad), 4.0 + m_bassEnergy * 2.5);
        corePen.setCapStyle(Qt::RoundCap);
        corePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(corePen);
        painter.setOpacity(1.0);
        painter.drawPath(pathMain);

        // 3. Secondary Yellow/Gold Dashed Wave Ribbon
        QLinearGradient dashGrad(0, 0, w, 0);
        dashGrad.setColorAt(0.0, m_glowColor);
        dashGrad.setColorAt(0.5, m_accentColor);
        dashGrad.setColorAt(1.0, m_glowColor);

        QPen dashPen(QBrush(dashGrad), 3.2 + m_bassEnergy * 1.8, Qt::DashLine);
        dashPen.setCapStyle(Qt::RoundCap);
        dashPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(dashPen);
        painter.setOpacity(0.92);
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

    void drawShockwaves(QPainter &painter)
    {
        for (const auto &sw : m_shockwaves) {
            QColor c = sw.color;
            c.setAlpha(static_cast<int>(sw.alpha * 255));
            painter.setPen(QPen(c, sw.strokeWidth + sw.alpha * 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(sw.x, sw.y), sw.radius, sw.radius);
        }
    }

    void drawSparkles(QPainter &painter)
    {
        for (const auto &s : m_sparkles) {
            QColor c = s.color;
            c.setAlpha(static_cast<int>(s.alpha * 255));
            painter.setPen(Qt::NoPen);
            painter.setBrush(c);
            painter.drawEllipse(QPointF(s.x, s.y), s.size, s.size);
        }
    }

private:
    QByteArray m_cavaBuffer;
    QTimer *m_timer = nullptr;
    QFileSystemWatcher *m_walWatcher = nullptr;
    QPoint m_dragPosition;
    double m_animTime = 0.0;
    int m_bassCooldown = 0;

    // CAVA Process
    QProcess *m_cavaProcess = nullptr;
    QString m_cavaConfigFile;

    // Frequencies
    int m_barsCount;
    std::vector<double> m_bands;
    std::vector<double> m_smoothBands;
    double m_bassEnergy = 0.0;
    double m_midEnergy = 0.0;
    double m_trebleEnergy = 0.0;

    // Screen Shake
    double m_shakeX = 0.0;
    double m_shakeY = 0.0;

    std::vector<Shockwave> m_shockwaves;
    std::vector<Sparkle> m_sparkles;

    QColor m_primaryColor;
    QColor m_secondaryColor;
    QColor m_glowColor;
    QColor m_accentColor;
};

// ============================================================================
// MAIN APPLICATION WINDOW (FRAMELESS & 100% TRANSPARENT)
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
        resize(950, 520);

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

    std::cout << "\n🌊 Ondas Visualizer ativo no terminal (Sem bordas / 100% Transparente)!" << std::endl;
    std::cout << "🎨 Cores sincronizadas automaticamente com seu Wallpaper (Pywal)." << std::endl;
    std::cout << "💡 Pressione 'Q', 'Esc' ou Ctrl+C para encerrar.\n" << std::endl;

    OndasMainWindow window;
    window.show();

    return app.exec();
}
