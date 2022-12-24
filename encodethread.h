#ifndef ENCODETHREAD_H
#define ENCODETHREAD_H

#include <QThread>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

typedef struct {
    int width;
    int height;
    AVPixelFormat fmt;
    int fps;
    const char *file;
} VideoEncodeSpec;

class EncodeThread : public QThread
{
    Q_OBJECT
private:
    void run() override;
public:
    EncodeThread(QObject *parent);
    ~EncodeThread();
signals:

};

#endif // ENCODETHREAD_H
