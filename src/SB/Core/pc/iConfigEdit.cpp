// Editing a config.ini in place. Why it works on lines is in iConfigEdit.h.

#include "iConfigEdit.h"

#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const S32 kMaxLines = 2048;
    const S32 kMaxLine = 512;
    const S32 kMaxName = 128;
    const S32 kMaxPath = 512;

    // One line, as it was read, without its newline. `section` is the section
    // in force at that line, so a lookup does not have to walk from the top,
    // and `key` is the name left of the '=' -- both empty for a line that is a
    // comment, a header or blank.
    struct Line
    {
        char text[kMaxLine];
        char section[kMaxName];
        char key[kMaxName];
        bool isHeader;
    };
} // namespace

struct iConfigEditFile
{
    Line lines[kMaxLines];
    S32 count;
    bool changed;

    // How the file ends its lines, so a save puts back what it read. Writing
    // CRLF into a file that used LF rewrites every line in it, which breaks
    // the one promise this module makes -- and turns a no-op save into a diff
    // touching the whole file.
    bool crlf;
};

namespace
{
    const char* skipSpace(const char* s)
    {
        while (*s == ' ' || *s == '\t')
        {
            s++;
        }
        return s;
    }

    // Copy `n` bytes and terminate, clamped to the destination.
    void copyN(char* out, size_t outSize, const char* s, size_t n)
    {
        if (n >= outSize)
        {
            n = outSize - 1;
        }
        memcpy(out, s, n);
        out[n] = '\0';
    }

    // Right-trim spaces and tabs in place.
    void rtrim(char* s)
    {
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        {
            n--;
        }
        s[n] = '\0';
    }

    // Where a line's comment starts, or NULL. The parser in iConfig.cpp takes
    // both ';' and '#', and a value is never quoted, so the first of either
    // ends the value -- which is also why a value containing one cannot be
    // written and is not a value the settings have.
    const char* commentAt(const char* s)
    {
        const char* semi = strchr(s, ';');
        const char* hash = strchr(s, '#');
        if (semi == NULL)
        {
            return hash;
        }
        if (hash == NULL)
        {
            return semi;
        }
        return semi < hash ? semi : hash;
    }

    // Fill in `section`, `key` and `isHeader` for a line already in `text`.
    // `section` comes in holding whatever section was in force above it and
    // goes out holding what is in force at it -- so a header line updates it.
    void classify(Line* line, char* section, size_t sectionSize)
    {
        line->key[0] = '\0';
        line->isHeader = false;

        // A comment is not a setting. The commented-out [pad] block is
        // deliberately left as comment text: those lines are a listing of what
        // the preset gives you, and turning one into a value is taking a
        // button over, which is a thing a front end does on purpose and not
        // something a save should do by accident.
        const char* s = skipSpace(line->text);
        if (*s == '\0' || *s == ';' || *s == '#')
        {
            snprintf(line->section, sizeof(line->section), "%s", section);
            return;
        }

        if (*s == '[')
        {
            const char* close = strchr(s, ']');
            if (close != NULL)
            {
                char name[kMaxName];
                const char* start = skipSpace(s + 1);
                copyN(name, sizeof(name), start, (size_t)(close - start));
                rtrim(name);
                snprintf(section, sectionSize, "%s", name);
                line->isHeader = true;
            }
            snprintf(line->section, sizeof(line->section), "%s", section);
            return;
        }

        snprintf(line->section, sizeof(line->section), "%s", section);

        const char* eq = strchr(s, '=');
        if (eq == NULL)
        {
            return;
        }

        copyN(line->key, sizeof(line->key), s, (size_t)(eq - s));
        rtrim(line->key);
    }

    // The line holding "section.name", or -1.
    S32 findLine(const iConfigEditFile* file, const char* section, const char* name)
    {
        for (S32 i = 0; i < file->count; i++)
        {
            const Line* line = &file->lines[i];
            if (line->key[0] != '\0' && iHostStrCaseCmp(line->section, section) == 0 &&
                iHostStrCaseCmp(line->key, name) == 0)
            {
                return i;
            }
        }
        return -1;
    }

    // The line after the last one belonging to `section`, or -1 when the file
    // has no such section. Trailing blank lines are left below the insertion,
    // so a value added to a section lands with the rest of it rather than
    // after the gap.
    S32 endOfSection(const iConfigEditFile* file, const char* section)
    {
        S32 last = -1;
        for (S32 i = 0; i < file->count; i++)
        {
            if (iHostStrCaseCmp(file->lines[i].section, section) == 0)
            {
                const char* s = skipSpace(file->lines[i].text);
                if (*s != '\0')
                {
                    last = i;
                }
            }
        }
        return last < 0 ? -1 : last + 1;
    }

    bool insertAt(iConfigEditFile* file, S32 at, const char* text, const char* section)
    {
        if (file->count >= kMaxLines)
        {
            return false;
        }
        if (at < 0 || at > file->count)
        {
            at = file->count;
        }

        for (S32 i = file->count; i > at; i--)
        {
            file->lines[i] = file->lines[i - 1];
        }
        file->count++;

        Line* line = &file->lines[at];
        memset(line, 0, sizeof(*line));
        snprintf(line->text, sizeof(line->text), "%s", text);

        char inForce[kMaxName];
        snprintf(inForce, sizeof(inForce), "%s", section);
        classify(line, inForce, sizeof(inForce));
        return true;
    }

    void removeAt(iConfigEditFile* file, S32 at)
    {
        for (S32 i = at; i + 1 < file->count; i++)
        {
            file->lines[i] = file->lines[i + 1];
        }
        file->count--;
    }
} // namespace

iConfigEditFile* iConfigEditNew()
{
    iConfigEditFile* file = (iConfigEditFile*)calloc(1, sizeof(iConfigEditFile));
    if (file != NULL)
    {
        // What a file written from nothing gets. A config.ini is read and
        // edited in Notepad as often as anywhere else.
        file->crlf = true;
    }
    return file;
}

iConfigEditFile* iConfigEditOpen(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        return NULL;
    }

    iConfigEditFile* file = iConfigEditNew();
    if (file == NULL)
    {
        fclose(f);
        return NULL;
    }

    char section[kMaxName];
    section[0] = '\0';

    bool sawEnding = false;

    char buffer[kMaxLine];
    while (file->count < kMaxLines && fgets(buffer, sizeof(buffer), f) != NULL)
    {
        size_t n = strlen(buffer);

        // The FIRST line ending decides, rather than a vote: a file with both
        // was written by two things, and following the one it opens with is at
        // least a rule someone can predict.
        if (!sawEnding && n > 0 && buffer[n - 1] == '\n')
        {
            file->crlf = (n > 1 && buffer[n - 2] == '\r');
            sawEnding = true;
        }

        while (n > 0 && (buffer[n - 1] == '\n' || buffer[n - 1] == '\r'))
        {
            buffer[--n] = '\0';
        }

        Line* line = &file->lines[file->count++];
        memset(line, 0, sizeof(*line));
        snprintf(line->text, sizeof(line->text), "%s", buffer);
        classify(line, section, sizeof(section));
    }

    bool overlong = (file->count >= kMaxLines && fgetc(f) != EOF);
    fclose(f);

    // Refusing is the only safe answer. Saving a file that was only partly
    // read would delete the tail of someone's config.ini, and a front end
    // cannot ask about a setting it never saw.
    if (overlong)
    {
        iConfigEditClose(file);
        return NULL;
    }

    return file;
}

void iConfigEditClose(iConfigEditFile* file)
{
    free(file);
}

const char* iConfigEditGet(const iConfigEditFile* file, const char* section, const char* name)
{
    if (file == NULL)
    {
        return NULL;
    }

    S32 at = findLine(file, section, name);
    if (at < 0)
    {
        return NULL;
    }

    // Into a buffer of its own rather than the line, which must keep its text.
    // One at a time is enough for how this is used -- a control reads its
    // value, then draws it -- and saying so here is better than handing back a
    // pointer into a line that the next Set rewrites.
    static char value[kMaxLine];

    const char* s = strchr(file->lines[at].text, '=');
    s = skipSpace(s + 1);

    const char* comment = commentAt(s);
    if (comment != NULL)
    {
        copyN(value, sizeof(value), s, (size_t)(comment - s));
    }
    else
    {
        snprintf(value, sizeof(value), "%s", s);
    }

    rtrim(value);
    return value;
}

bool iConfigEditSet(iConfigEditFile* file, const char* section, const char* name,
                    const char* value)
{
    if (file == NULL)
    {
        return false;
    }

    S32 at = findLine(file, section, name);
    if (at < 0)
    {
        // A section the file does not have gets its header first, with a blank
        // line above it so it does not run into whatever it follows.
        S32 insert = endOfSection(file, section);
        if (insert < 0)
        {
            char header[kMaxName + 4];
            snprintf(header, sizeof(header), "[%s]", section);
            if (!insertAt(file, file->count, "", section) ||
                !insertAt(file, file->count, header, ""))
            {
                return false;
            }
            insert = file->count;
        }

        char text[kMaxLine];
        snprintf(text, sizeof(text), "%s = %s", name, value);
        if (!insertAt(file, insert, text, section))
        {
            return false;
        }

        file->changed = true;
        return true;
    }

    Line* line = &file->lines[at];

    // Everything up to where the value starts, then the new value, then the
    // trailing comment if the line had one. The left-hand side is copied
    // rather than rebuilt so that whatever spacing or capitalisation the line
    // was written with survives -- including the space around the '=', which
    // is not the same on every line of a file people hand-edit, and the column
    // alignment a generated file uses.
    const char* eq = strchr(line->text, '=');
    const char* after = skipSpace(eq + 1);
    const char* comment = commentAt(after);

    char rebuilt[kMaxLine];
    copyN(rebuilt, sizeof(rebuilt), line->text, (size_t)(after - line->text));

    size_t at2 = strlen(rebuilt);
    snprintf(rebuilt + at2, sizeof(rebuilt) - at2, "%s", value);

    if (comment != NULL)
    {
        // Kept at the column it was at, when the new value is short enough to
        // leave it there. A generated file lines its comments up and a value
        // changing by a character should not shuffle them.
        size_t want = (size_t)(comment - line->text);
        size_t have = strlen(rebuilt);
        while (have < want && have + 1 < sizeof(rebuilt))
        {
            rebuilt[have++] = ' ';
        }
        rebuilt[have] = '\0';

        if (have == want)
        {
            snprintf(rebuilt + have, sizeof(rebuilt) - have, "%s", comment);
        }
        else
        {
            snprintf(rebuilt + have, sizeof(rebuilt) - have, " %s", comment);
        }
    }

    if (strcmp(rebuilt, line->text) != 0)
    {
        snprintf(line->text, sizeof(line->text), "%s", rebuilt);
        file->changed = true;
    }

    return true;
}

void iConfigEditUnset(iConfigEditFile* file, const char* section, const char* name)
{
    if (file == NULL)
    {
        return;
    }

    S32 at = findLine(file, section, name);
    if (at < 0)
    {
        return;
    }

    removeAt(file, at);
    file->changed = true;
}

bool iConfigEditChanged(const iConfigEditFile* file)
{
    return file != NULL && file->changed;
}

bool iConfigEditSave(const iConfigEditFile* file, const char* path)
{
    if (file == NULL)
    {
        return false;
    }

    char temp[kMaxPath];
    snprintf(temp, sizeof(temp), "%s.new", path);

    // Not iHostCreateNewFile: a leftover .new from an interrupted save would
    // then block every save after it, and there is nothing in one worth
    // keeping.
    FILE* f = fopen(temp, "wb");
    if (f == NULL)
    {
        return false;
    }

    // Whatever the file already used. The parser trims both either way, so
    // this is not about being read back -- it is that rewriting every line of
    // someone's file to change one value is not an edit anybody asked for.
    const char* ending = file->crlf ? "\r\n" : "\n";
    for (S32 i = 0; i < file->count; i++)
    {
        fprintf(f, "%s%s", file->lines[i].text, ending);
    }

    if (fclose(f) != 0)
    {
        remove(temp);
        return false;
    }

    if (!iHostRenameReplace(temp, path))
    {
        remove(temp);
        return false;
    }

    return true;
}
