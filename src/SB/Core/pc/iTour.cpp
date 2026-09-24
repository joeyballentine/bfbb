#include "iTour.h"

#include "iFrameGrab.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iSnapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    enum Op
    {
        OP_WAIT,
        OP_HOLD,
        OP_SHOT,
        OP_QUIT
    };

    struct Step
    {
        Op op;
        S32 frames;
        U32 buttons;
        char name[64];
    };

    const S32 kMaxSteps = 1024;
    const S32 kRelease = 4;
    const S32 kShotWidth = 1280;

    // A shot taken while a loading screen holds the snapshot has no frame to
    // read; it is retried for this many frames before giving up.
    const S32 kShotRetry = 600;

    Step sSteps[kMaxSteps];
    S32 sCount;
    S32 sAt;
    S32 sLeft;
    S32 sReleasing;
    char sOutDir[512];

    void Fail(S32 line, const char* what, const char* text)
    {
        printf("bfbb: tour: line %d: %s: %s\n", (int)line, what, text);
        fflush(stdout);
        exit(1);
    }

    // Space-separated button names from `p` into a mask.
    U32 Buttons(char* p, S32 line)
    {
        U32 mask = 0;
        for (char* tok = strtok(p, " \t"); tok != NULL; tok = strtok(NULL, " \t"))
        {
            const iPadBindButton* b = iPadBindFind(tok);
            if (b == NULL)
            {
                Fail(line, "unknown button", tok);
            }
            mask |= b->mask;
        }
        if (mask == 0)
        {
            Fail(line, "no buttons", "");
        }
        return mask;
    }

    void Load(const char* path)
    {
        FILE* f = fopen(path, "r");
        if (f == NULL)
        {
            printf("bfbb: tour: cannot open %s\n", path);
            fflush(stdout);
            exit(1);
        }

        char text[512];
        S32 line = 0;
        while (fgets(text, sizeof(text), f) != NULL)
        {
            line++;

            char* hash = strchr(text, '#');
            if (hash != NULL)
            {
                *hash = '\0';
            }
            text[strcspn(text, "\r\n")] = '\0';

            char* p = text;
            while (*p == ' ' || *p == '\t')
            {
                p++;
            }
            if (*p == '\0')
            {
                continue;
            }

            if (sCount == kMaxSteps)
            {
                Fail(line, "too many steps", "");
            }

            char* cmd = p;
            p += strcspn(p, " \t");
            if (*p != '\0')
            {
                *p++ = '\0';
            }

            Step* s = &sSteps[sCount];
            memset(s, 0, sizeof(*s));

            if (strcmp(cmd, "wait") == 0)
            {
                s->op = OP_WAIT;
                s->frames = atoi(p);
            }
            else if (strcmp(cmd, "press") == 0)
            {
                s->op = OP_HOLD;
                s->frames = 4;
                s->buttons = Buttons(p, line);
            }
            else if (strcmp(cmd, "hold") == 0)
            {
                s->op = OP_HOLD;
                s->frames = (S32)strtol(p, &p, 10);
                s->buttons = Buttons(p, line);
            }
            else if (strcmp(cmd, "shot") == 0)
            {
                s->op = OP_SHOT;
                s->frames = kShotRetry;
                if (*p == '\0' || strlen(p) >= sizeof(s->name))
                {
                    Fail(line, "shot needs a short name", p);
                }
                strcpy(s->name, p);
            }
            else if (strcmp(cmd, "quit") == 0)
            {
                s->op = OP_QUIT;
            }
            else
            {
                Fail(line, "unknown command", cmd);
            }

            sCount++;
        }

        fclose(f);
    }

    void Begin()
    {
        if (sAt >= sCount)
        {
            printf("bfbb: tour: done\n");
            fflush(stdout);
            exit(0);
        }

        const Step* s = &sSteps[sAt];
        sLeft = s->frames;
        sReleasing = 0;
        iPadHostScript(TRUE, s->op == OP_HOLD ? s->buttons : 0);
    }

    void Next()
    {
        sAt++;
        Begin();
    }

    void Frame()
    {
        const Step* s = &sSteps[sAt];

        switch (s->op)
        {
        case OP_WAIT:
            if (--sLeft <= 0)
            {
                Next();
            }
            break;

        case OP_HOLD:
            if (--sLeft > 0)
            {
                break;
            }
            if (!sReleasing)
            {
                sReleasing = 1;
                sLeft = kRelease;
                iPadHostScript(TRUE, 0);
                break;
            }
            Next();
            break;

        case OP_SHOT:
        {
            char path[640];
            snprintf(path, sizeof(path), "%s%s.png", sOutDir, s->name);
            if (iFrameGrabWrite(path, kShotWidth))
            {
                printf("bfbb: tour: wrote %s\n", path);
                fflush(stdout);
                Next();
            }
            else if (--sLeft <= 0)
            {
                printf("bfbb: tour: no frame for %s\n", s->name);
                fflush(stdout);
                Next();
            }
            break;
        }

        case OP_QUIT:
            printf("bfbb: tour: quit\n");
            fflush(stdout);
            exit(0);
        }
    }
} // namespace

S32 iTourActive()
{
    const char* path = getenv("BFBB_TOUR");
    return path != NULL && path[0] != '\0';
}

void iTourInit()
{
    if (!iTourActive())
    {
        return;
    }

    const char* script = getenv("BFBB_TOUR");
    Load(script);

    const char* out = getenv("BFBB_TOUR_OUT");
    if (out != NULL && out[0] != '\0')
    {
        snprintf(sOutDir, sizeof(sOutDir), "%s/", out);
    }
    else
    {
        // The script's folder, with its separator.
        const char* slash = strrchr(script, '/');
        const char* back = strrchr(script, '\\');
        if (back != NULL && (slash == NULL || back > slash))
        {
            slash = back;
        }
        size_t n = slash != NULL ? (size_t)(slash - script + 1) : 0;
        if (n >= sizeof(sOutDir))
        {
            n = 0;
        }
        memcpy(sOutDir, script, n);
        sOutDir[n] = '\0';
    }

    printf("bfbb: tour: %d steps from %s, shots to %s\n", (int)sCount, script,
           sOutDir[0] != '\0' ? sOutDir : "the current folder");
    fflush(stdout);

    sAt = 0;
    Begin();
    iSnapshotSetFrameHook(Frame);
}
