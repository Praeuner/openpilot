// bp_hardware_video_test.cc
// Simple test to verify hardware video decoder functionality

#include "bp_hardware_video_decoder.h"
#include <QApplication>
#include <QDebug>
#include <QTimer>

class HardwareVideoTest : public QObject {
    Q_OBJECT

public:
    HardwareVideoTest() {
        decoder = new BPHardwareVideoDecoder(this);

        // Connect signals
        connect(decoder, &BPHardwareVideoDecoder::durationChanged, this, &HardwareVideoTest::onDurationChanged);
        connect(decoder, &BPHardwareVideoDecoder::positionChanged, this, &HardwareVideoTest::onPositionChanged);
        connect(decoder, &BPHardwareVideoDecoder::playbackFinished, this, &HardwareVideoTest::onPlaybackFinished);
        connect(decoder, &BPHardwareVideoDecoder::errorOccurred, this, &HardwareVideoTest::onErrorOccurred);
    }

    void testDecoder(const QString &videoPath) {
        qDebug() << "Testing hardware video decoder with:" << videoPath;

        if (!decoder->initialize(videoPath)) {
            qDebug() << "Failed to initialize decoder";
            return;
        }

        qDebug() << "Decoder initialized successfully";
        qDebug() << "Duration:" << decoder->duration() << "ms";

        // Start playback
        decoder->play();
        qDebug() << "Playback started";
    }

private slots:
    void onDurationChanged(qint64 duration) {
        qDebug() << "Duration changed:" << duration << "ms";
    }

    void onPositionChanged(qint64 position) {
        static int lastSecond = -1;
        int currentSecond = position / 1000;
        if (currentSecond != lastSecond) {
            qDebug() << "Position:" << currentSecond << "s";
            lastSecond = currentSecond;
        }
    }

    void onPlaybackFinished() {
        qDebug() << "Playback finished";
        QApplication::quit();
    }

    void onErrorOccurred(const QString &error) {
        qDebug() << "Error occurred:" << error;
        QApplication::quit();
    }

private:
    BPHardwareVideoDecoder *decoder;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    if (argc < 2) {
        qDebug() << "Usage:" << argv[0] << "<video_file_path>";
        return 1;
    }

    HardwareVideoTest test;
    test.testDecoder(QString::fromUtf8(argv[1]));

    return app.exec();
}

#include "bp_hardware_video_test.moc"
