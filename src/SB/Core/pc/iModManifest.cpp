// Heavy Mod Manager's mod.json. See iModManifest.h.

#include "iModManifest.h"

#include "iHost.h"
#include "iSaveFile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const char* const kManifest = "mod.json";

    // Just enough JSON for mod.json: one object whose values the reader either
    // keeps, when they are strings it wants, or steps over.
    struct Json
    {
        const char* at;
        const char* end;

        void space()
        {
            while (at < end && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n'))
            {
                at++;
            }
        }

        bool take(char c)
        {
            space();
            if (at < end && *at == c)
            {
                at++;
                return true;
            }
            return false;
        }

        static void putUtf8(char* out, size_t* n, U32 cp)
        {
            if (cp < 0x80)
            {
                out[(*n)++] = (char)cp;
            }
            else if (cp < 0x800)
            {
                out[(*n)++] = (char)(0xC0 | (cp >> 6));
                out[(*n)++] = (char)(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out[(*n)++] = (char)(0xE0 | (cp >> 12));
                out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[(*n)++] = (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                out[(*n)++] = (char)(0xF0 | (cp >> 18));
                out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[(*n)++] = (char)(0x80 | (cp & 0x3F));
            }
        }

        bool hex4(U32* out)
        {
            if (end - at < 4)
            {
                return false;
            }

            U32 v = 0;
            for (int i = 0; i < 4; i++)
            {
                char c = at[i];
                v <<= 4;
                if (c >= '0' && c <= '9')
                {
                    v |= (U32)(c - '0');
                }
                else if (c >= 'a' && c <= 'f')
                {
                    v |= (U32)(c - 'a' + 10);
                }
                else if (c >= 'A' && c <= 'F')
                {
                    v |= (U32)(c - 'A' + 10);
                }
                else
                {
                    return false;
                }
            }
            at += 4;
            *out = v;
            return true;
        }

        // A string, decoded into malloc'd UTF-8 when `out` is not NULL. An escape
        // never decodes to more bytes than it was written with, so the encoded
        // length is enough room.
        bool string(char** out)
        {
            if (!take('"'))
            {
                return false;
            }

            const char* start = at;
            char* text = out != NULL ? (char*)malloc((size_t)(end - start) + 1) : NULL;
            if (out != NULL && text == NULL)
            {
                return false;
            }
            size_t n = 0;

            while (at < end && *at != '"')
            {
                char c = *at++;
                if (c != '\\')
                {
                    if (text != NULL)
                    {
                        text[n++] = c;
                    }
                    continue;
                }

                if (at >= end)
                {
                    break;
                }

                char e = *at++;
                U32 cp;
                switch (e)
                {
                case 'b':
                    cp = '\b';
                    break;
                case 'f':
                    cp = '\f';
                    break;
                case 'n':
                    cp = '\n';
                    break;
                case 'r':
                    cp = '\r';
                    break;
                case 't':
                    cp = '\t';
                    break;
                case 'u':
                    if (!hex4(&cp))
                    {
                        free(text);
                        return false;
                    }
                    if (cp >= 0xD800 && cp < 0xDC00 && end - at >= 6 && at[0] == '\\' &&
                        at[1] == 'u')
                    {
                        const char* mark = at;
                        U32 low;
                        at += 2;
                        if (hex4(&low) && low >= 0xDC00 && low < 0xE000)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        else
                        {
                            at = mark;
                        }
                    }
                    break;
                default:
                    cp = (U8)e;
                    break;
                }

                if (text != NULL)
                {
                    putUtf8(text, &n, cp);
                }
            }

            if (at >= end)
            {
                free(text);
                return false;
            }
            at++;

            if (out != NULL)
            {
                text[n] = '\0';
                *out = text;
            }
            return true;
        }

        bool value()
        {
            space();
            if (at >= end)
            {
                return false;
            }

            if (*at == '"')
            {
                return string(NULL);
            }

            if (*at == '{' || *at == '[')
            {
                const char close = *at == '{' ? '}' : ']';
                at++;
                if (take(close))
                {
                    return true;
                }
                do
                {
                    if (close == '}' && !(string(NULL) && take(':')))
                    {
                        return false;
                    }
                    if (!value())
                    {
                        return false;
                    }
                } while (take(','));
                return take(close);
            }

            // A number, true, false or null.
            const char* start = at;
            while (at < end && *at != ',' && *at != '}' && *at != ']' && *at != ' ' &&
                   *at != '\t' && *at != '\r' && *at != '\n')
            {
                at++;
            }
            return at > start;
        }
    };

    bool readFile(const char* path, char** out, size_t* size)
    {
        FILE* fp = fopen(path, "rb");
        if (fp == NULL)
        {
            return false;
        }

        fseek(fp, 0, SEEK_END);
        long length = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        char* text = length >= 0 ? (char*)malloc((size_t)length + 1) : NULL;
        bool ok = text != NULL && fread(text, 1, (size_t)length, fp) == (size_t)length;
        fclose(fp);

        if (!ok)
        {
            free(text);
            return false;
        }

        text[length] = '\0';
        *out = text;
        *size = (size_t)length;
        return true;
    }

    bool parseManifest(const char* text, size_t size, iModManifest* out)
    {
        Json json = { text, text + size };

        // A UTF-8 byte order mark, which .NET writes by default.
        if (size >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0)
        {
            json.at += 3;
        }

        if (!json.take('{'))
        {
            return false;
        }
        if (json.take('}'))
        {
            return true;
        }

        static const char* const kPatches[] = { "IpsPatchBase64", "DOLPatches", "ArCodes",
                                                "GeckoCodes" };

        do
        {
            char* key;
            if (!json.string(&key))
            {
                return false;
            }
            if (!json.take(':'))
            {
                free(key);
                return false;
            }

            char** keep = NULL;
            bool patch = false;
            if (strcmp(key, "MergeFiles") == 0)
            {
                keep = &out->mergeFiles;
            }
            else if (strcmp(key, "INIReplacements") == 0)
            {
                keep = &out->iniReplacements;
            }
            for (size_t i = 0; i < sizeof(kPatches) / sizeof(kPatches[0]); i++)
            {
                patch = patch || strcmp(key, kPatches[i]) == 0;
            }
            free(key);

            json.space();
            if ((keep != NULL || patch) && json.at < json.end && *json.at == '"')
            {
                char* value;
                if (!json.string(&value))
                {
                    return false;
                }

                if (patch)
                {
                    out->patchesExecutable = out->patchesExecutable || value[0] != '\0';
                    free(value);
                }
                else
                {
                    free(*keep);
                    *keep = value;
                }
            }
            else if (!json.value())
            {
                return false;
            }
        } while (json.take(','));

        return json.take('}');
    }

    const char* skipBlank(const char* p, const char* end)
    {
        while (p < end && (*p == ' ' || *p == '\t'))
        {
            p++;
        }
        return p;
    }

    const char* trimBlank(const char* start, const char* p)
    {
        while (p > start && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\r'))
        {
            p--;
        }
        return p;
    }

    // One `key = value` line of an INI, comment removed and blanks trimmed.
    struct IniLine
    {
        const char* key;
        size_t keyLength;
        const char* value;
        size_t valueLength;
    };

    bool parseIniLine(const char* line, const char* end, IniLine* out)
    {
        const char* hash = (const char*)memchr(line, '#', (size_t)(end - line));
        if (hash != NULL)
        {
            end = hash;
        }

        const char* equals = (const char*)memchr(line, '=', (size_t)(end - line));
        if (equals == NULL)
        {
            return false;
        }

        const char* key = skipBlank(line, equals);
        const char* keyEnd = trimBlank(key, equals);
        const char* value = skipBlank(equals + 1, end);
        const char* valueEnd = trimBlank(value, end);
        if (keyEnd == key)
        {
            return false;
        }

        out->key = key;
        out->keyLength = (size_t)(keyEnd - key);
        out->value = value;
        out->valueLength = (size_t)(valueEnd - value);
        return true;
    }

    bool sameNoCase(const char* a, const char* b, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool keyIs(const IniLine& line, const char* key)
    {
        return line.keyLength == strlen(key) && sameNoCase(line.key, key, line.keyLength);
    }

    // The value's first word, for the keys SB.INI lists once per scene.
    size_t codeLength(const IniLine& line)
    {
        static const char* const kCodeKeys[] = { "ScenePlayerMapping", "ThresholdPointsRange",
                                                 "AlternateCostumeMapping" };

        for (size_t i = 0; i < sizeof(kCodeKeys) / sizeof(kCodeKeys[0]); i++)
        {
            if (keyIs(line, kCodeKeys[i]))
            {
                size_t n = 0;
                while (n < line.valueLength && line.value[n] != ' ' && line.value[n] != '\t')
                {
                    n++;
                }
                return n;
            }
        }
        return 0;
    }

    bool sameSetting(const IniLine& a, const IniLine& b)
    {
        if (a.keyLength != b.keyLength || !sameNoCase(a.key, b.key, a.keyLength))
        {
            return false;
        }

        size_t code = codeLength(a);
        return code == codeLength(b) && sameNoCase(a.value, b.value, code);
    }
} // namespace

bool iModManifestPath(const char* folder, char* out, size_t outSize)
{
    snprintf(out, outSize, "%s/%s", folder, kManifest);
    if (iHostPathExists(out))
    {
        return true;
    }

    char leaf[256];
    iSaveFileLeaf(folder, leaf, sizeof(leaf));
    if (iHostStrCaseCmp(leaf, "files") != 0)
    {
        return false;
    }

    snprintf(out, outSize, "%s/../%s", folder, kManifest);
    return iHostPathExists(out);
}

bool iModManifestRead(const char* folder, iModManifest* out)
{
    memset(out, 0, sizeof(*out));

    char path[1024];
    if (!iModManifestPath(folder, path, sizeof(path)))
    {
        return false;
    }

    char* text;
    size_t size;
    if (!readFile(path, &text, &size))
    {
        printf("bfbb: mod: could not read %s\n", path);
        return false;
    }

    bool ok = parseManifest(text, size, out);
    free(text);

    if (!ok)
    {
        printf("bfbb: mod: %s is not valid JSON; ignored\n", path);
        iModManifestFree(out);
        return false;
    }
    return true;
}

void iModManifestFree(iModManifest* manifest)
{
    free(manifest->mergeFiles);
    free(manifest->iniReplacements);
    memset(manifest, 0, sizeof(*manifest));
}

bool iModManifestMerges(const iModManifest* manifest, const char* rel)
{
    if (manifest->mergeFiles == NULL)
    {
        return false;
    }

    const size_t relLength = strlen(rel);
    const char* line = manifest->mergeFiles;
    while (*line != '\0')
    {
        const char* end = strchr(line, '\n');
        if (end == NULL)
        {
            end = line + strlen(line);
        }

        const char* a = skipBlank(line, end);
        const char* b = trimBlank(a, end);
        if ((size_t)(b - a) == relLength)
        {
            size_t i = 0;
            for (; i < relLength; i++)
            {
                char x = a[i] == '\\' ? '/' : (char)tolower((unsigned char)a[i]);
                char y = rel[i] == '\\' ? '/' : (char)tolower((unsigned char)rel[i]);
                if (x != y)
                {
                    break;
                }
            }
            if (i == relLength)
            {
                return true;
            }
        }

        line = *end == '\0' ? end : end + 1;
    }
    return false;
}

char* iModManifestApplyIni(const char* ini, size_t iniSize, const char* replacements,
                           size_t* outSize)
{
    const char* replEnd = replacements + strlen(replacements);

    // The replacements, collected once.
    S32 count = 0;
    for (const char* p = replacements; p < replEnd; p++)
    {
        count += (*p == '\n');
    }
    count++;

    IniLine* repl = (IniLine*)malloc((size_t)count * sizeof(IniLine));
    if (repl == NULL)
    {
        return NULL;
    }

    S32 used = 0;
    size_t room = iniSize + 2;
    for (const char* line = replacements; line < replEnd;)
    {
        const char* end = (const char*)memchr(line, '\n', (size_t)(replEnd - line));
        if (end == NULL)
        {
            end = replEnd;
        }

        if (parseIniLine(line, end, &repl[used]))
        {
            room += repl[used].keyLength + repl[used].valueLength + 2;
            used++;
        }
        line = end + 1;
    }

    char* out = (char*)malloc(room + 1);
    if (out == NULL)
    {
        free(repl);
        return NULL;
    }

    size_t n = 0;
    const char* iniEnd = ini + iniSize;
    for (const char* line = ini; line < iniEnd;)
    {
        const char* end = (const char*)memchr(line, '\n', (size_t)(iniEnd - line));
        const char* next = end != NULL ? end + 1 : iniEnd;

        IniLine setting;
        bool replaced = false;
        if (parseIniLine(line, end != NULL ? end : iniEnd, &setting))
        {
            for (S32 i = 0; i < used && !replaced; i++)
            {
                replaced = sameSetting(setting, repl[i]);
            }
        }

        if (!replaced)
        {
            memcpy(out + n, line, (size_t)(next - line));
            n += (size_t)(next - line);
        }
        line = next;
    }

    if (n > 0 && out[n - 1] != '\n')
    {
        out[n++] = '\n';
    }

    // Values with their runs of blanks made single spaces, as the manager
    // writes them.
    for (S32 i = 0; i < used; i++)
    {
        memcpy(out + n, repl[i].key, repl[i].keyLength);
        n += repl[i].keyLength;
        out[n++] = '=';

        bool blank = false;
        for (size_t k = 0; k < repl[i].valueLength; k++)
        {
            char c = repl[i].value[k];
            if (c == ' ' || c == '\t')
            {
                blank = true;
                continue;
            }
            if (blank)
            {
                out[n++] = ' ';
                blank = false;
            }
            out[n++] = c;
        }
        out[n++] = '\n';
    }

    free(repl);
    out[n] = '\0';
    *outSize = n;
    return out;
}
