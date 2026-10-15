// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// A small PDF writer for LiveAlign's reports: US Letter or A4 pages (portrait or landscape) with text in the standard
// Helvetica and Courier fonts, lines, rectangles and JPEG pictures. Coordinates are in points from the top-left corner
// of the page. Drawing goes to the newest page unless an earlier one is picked (for contents and "page x of y").
#include <vector>
#include <juce_graphics/juce_graphics.h>

namespace la::pdf
{
class Document
{
public:
    // US Letter, or A4 (Settings, Reports and files): set before a document is made.
    static bool& useA4() { static bool a4 = false; return a4; }
    const float kPageW = useA4() ? 595.28f : 612.0f, kPageH = useA4() ? 841.89f : 792.0f;
    enum FontId { Regular = 0, Bold = 1, Mono = 2, Italic = 3 };

    void newPage (bool landscape = false)
    {
        Page pg;
        pg.landscape = landscape;
        pages.push_back (std::move (pg));
        target = -1;
    }
    int pageCount() const { return (int) pages.size(); }
    // Draw on an earlier page (0-based); -1 = the newest page again.
    void selectPage (int index) { target = index >= 0 && index < (int) pages.size() ? index : -1; }
    float pageW() const { return curPage().landscape ? kPageH : kPageW; }
    float pageH() const { return curPage().landscape ? kPageW : kPageH; }

    // Text width in points, from the standard Helvetica metrics (the PDF fonts themselves).
    static float textWidth (const juce::String& t, float size, FontId f = Regular)
    {
        if (f == Mono) return (float) t.length() * size * 0.6f;
        static const short reg[95] = { 278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,556,500,722,500,500,500,334,260,334,584 };
        static const short bold[95] = { 278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,611,611,389,556,333,611,556,778,556,556,500,389,280,389,584 };
        const short* w = f == Bold ? bold : reg;
        int units = 0;
        for (auto p = t.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            units += c >= 32 && c < 127 ? w[c - 32] : 556;
        }
        return (float) units * size / 1000.0f;
    }

    void text (float x, float y, const juce::String& t, float size, FontId f = Regular, juce::Colour c = juce::Colours::black)
    {
        auto& s = cur();
        s << "BT /F" << (int) f << ' ' << num (size) << " Tf " << rgb (c, false) << ' ' << num (x) << ' ' << num (pageH() - y) << " Td ("
          << escape (t) << ") Tj ET\n";
    }

    void textRight (float right, float y, const juce::String& t, float size, FontId f = Regular, juce::Colour c = juce::Colours::black)
    {
        text (right - textWidth (t, size, f), y, t, size, f, c);
    }

    // Wraps `t` to `width`; returns the height used.
    float paragraph (float x, float y, float width, const juce::String& t, float size, FontId f = Regular,
                     juce::Colour c = juce::Colours::black, float leading = 1.35f)
    {
        float yy = y;
        for (const auto& line : wrap (t, width, size, f))
        {
            text (x, yy, line, size, f, c);
            yy += size * leading;
        }
        return yy - y;
    }

    static juce::StringArray wrap (const juce::String& t, float width, float size, FontId f = Regular)
    {
        juce::StringArray out, paras;
        paras.addLines (t);
        for (const auto& para : paras)
        {
            juce::StringArray words;
            words.addTokens (para, " ", {});
            juce::String line;
            for (const auto& w : words)
            {
                const auto trial = line.isEmpty() ? w : line + " " + w;
                if (line.isNotEmpty() && textWidth (trial, size, f) > width) { out.add (line); line = w; }
                else line = trial;
            }
            out.add (line);
        }
        return out;
    }

    void line (float x1, float y1, float x2, float y2, float width, juce::Colour c)
    {
        const float h = pageH();
        cur() << num (width) << " w " << rgb (c, true) << ' ' << num (x1) << ' ' << num (h - y1) << " m " << num (x2) << ' '
              << num (h - y2) << " l S\n";
    }

    void rect (float x, float y, float w, float h, juce::Colour fill, juce::Colour stroke = {}, float strokeWidth = 0.6f)
    {
        auto& s = cur();
        const bool doFill = ! fill.isTransparent(), doStroke = ! stroke.isTransparent();
        if (doFill) s << rgb (fill, false) << ' ';
        if (doStroke) s << num (strokeWidth) << " w " << rgb (stroke, true) << ' ';
        s << num (x) << ' ' << num (pageH() - y - h) << ' ' << num (w) << ' ' << num (h) << " re " << (doFill && doStroke ? "B" : doFill ? "f" : "S") << "\n";
    }

    // A picture, stored as JPEG.
    void image (const juce::Image& img, float x, float y, float w, float h, float quality = 0.92f)
    {
        juce::MemoryOutputStream jpg;
        juce::JPEGImageFormat fmt;
        fmt.setQuality (quality);
        fmt.writeImageToStream (img.convertedToFormat (juce::Image::RGB), jpg);
        images.push_back ({ jpg.getMemoryBlock(), img.getWidth(), img.getHeight() });
        const int id = (int) images.size() - 1;
        (target >= 0 ? pages[(size_t) target] : pages.back()).images.push_back (id);
        cur() << "q " << num (w) << " 0 0 " << num (h) << ' ' << num (x) << ' ' << num (pageH() - y - h) << " cm /Im" << id << " Do Q\n";
    }

    juce::MemoryBlock build (const juce::String& title, const juce::String& author) const
    {
        juce::MemoryOutputStream out;
        std::vector<juce::int64> offsets;
        auto beginObj = [&] { offsets.push_back (out.getPosition()); out << (int) offsets.size() << " 0 obj\n"; };
        auto endObj = [&] { out << "endobj\n"; };

        out << "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
        // 1 catalog, 2 pages, 3..6 fonts, 7 info; then images, then each page and its content
        const int firstImage = 8;
        const int firstPage = firstImage + (int) images.size();
        beginObj(); out << "<< /Type /Catalog /Pages 2 0 R >>\n"; endObj();
        beginObj();
        out << "<< /Type /Pages /Count " << (int) pages.size() << " /Kids [";
        for (int i = 0; i < (int) pages.size(); ++i) out << (firstPage + i * 2) << " 0 R ";
        out << "] >>\n";
        endObj();
        for (const char* f : { "Helvetica", "Helvetica-Bold", "Courier", "Helvetica-Oblique" })
        {
            beginObj();
            out << "<< /Type /Font /Subtype /Type1 /BaseFont /" << f << " /Encoding /WinAnsiEncoding >>\n";
            endObj();
        }
        beginObj();
        out << "<< /Title (" << escape (title) << ") /Author (" << escape (author) << ") /Producer (LiveAlign) /CreationDate (D:"
            << juce::Time::getCurrentTime().formatted ("%Y%m%d%H%M%S") << ") >>\n";
        endObj();

        for (const auto& im : images)
        {
            beginObj();
            out << "<< /Type /XObject /Subtype /Image /Width " << im.w << " /Height " << im.h
                << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " << (int) im.jpeg.getSize() << " >>\nstream\n";
            out.write (im.jpeg.getData(), im.jpeg.getSize());
            out << "\nendstream\n";
            endObj();
        }

        for (int i = 0; i < (int) pages.size(); ++i)
        {
            const auto& pg = pages[(size_t) i];
            beginObj();
            out << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << num (pg.landscape ? kPageH : kPageW) << ' ' << num (pg.landscape ? kPageW : kPageH) << "] /Contents "
                << (firstPage + i * 2 + 1) << " 0 R /Resources << /Font << /F0 3 0 R /F1 4 0 R /F2 5 0 R /F3 6 0 R >>";
            if (! pg.images.empty())
            {
                out << " /XObject <<";
                for (int id : pg.images) out << " /Im" << id << ' ' << (firstImage + id) << " 0 R";
                out << " >>";
            }
            out << " >> >>\n";
            endObj();
            const auto bytes = encode (pg.content);
            beginObj();
            out << "<< /Length " << (int) bytes.getSize() << " >>\nstream\n";
            out.write (bytes.getData(), bytes.getSize());
            out << "\nendstream\n";
            endObj();
        }

        const auto xref = out.getPosition();
        out << "xref\n0 " << (int) offsets.size() + 1 << "\n0000000000 65535 f \n";
        for (auto o : offsets) out << juce::String (o).paddedLeft ('0', 10) << " 00000 n \n";
        out << "trailer\n<< /Size " << (int) offsets.size() + 1 << " /Root 1 0 R /Info 7 0 R >>\nstartxref\n" << (int) xref << "\n%%EOF\n";
        return out.getMemoryBlock();
    }

    bool write (const juce::File& f, const juce::String& title, const juce::String& author) const
    {
        const auto data = build (title, author);
        return f.replaceWithData (data.getData(), data.getSize());
    }

private:
    struct Page { juce::String content; std::vector<int> images; bool landscape = false; };
    struct Img { juce::MemoryBlock jpeg; int w, h; };

    juce::String& cur()
    {
        if (pages.empty()) newPage();
        return (target >= 0 ? pages[(size_t) target] : pages.back()).content;
    }

    const Page& curPage() const
    {
        static const Page none;
        if (pages.empty()) return none;
        return target >= 0 ? pages[(size_t) target] : pages.back();
    }

    static juce::String num (float v) { return juce::String (v, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); }

    static juce::String rgb (juce::Colour c, bool stroke)
    {
        return num (c.getFloatRed()) + ' ' + num (c.getFloatGreen()) + ' ' + num (c.getFloatBlue()) + (stroke ? " RG" : " rg");
    }

    // Text for a string literal in WinAnsi (Latin-1 plus a few typographic characters).
    static juce::String escape (const juce::String& t)
    {
        juce::String s;
        for (auto p = t.getCharPointer(); ! p.isEmpty();)
        {
            const juce::juce_wchar c = p.getAndAdvance();
            if (c == '(' || c == ')' || c == '\\') { s << '\\' << (char) c; continue; }
            if (c < 32) continue;
            if (c < 128) { s << (char) c; continue; }
            int b = -1;
            switch (c)
            {
                case 0x2013: b = 0x96; break;   // en dash
                case 0x2014: b = 0x97; break;   // em dash
                case 0x2022: b = 0x95; break;   // bullet
                case 0x2026: b = 0x85; break;   // ellipsis
                case 0x2018: b = 0x91; break;
                case 0x2019: b = 0x92; break;
                case 0x201c: b = 0x93; break;
                case 0x201d: b = 0x94; break;
                case 0x2192: s << "->"; continue;
                case 0x2190: s << "<-"; continue;
                case 0x221e: s << "inf"; continue;
                default: b = c < 256 ? (int) c : '?'; break;
            }
            s << juce::String::formatted ("\\%03o", b);
        }
        return s;
    }

    static juce::MemoryBlock encode (const juce::String& content)
    {
        // content streams are plain ASCII (all other bytes were escaped as octal)
        juce::MemoryBlock b;
        b.append (content.toRawUTF8(), content.getNumBytesAsUTF8());
        return b;
    }

    std::vector<Page> pages;
    std::vector<Img> images;
    int target = -1;
};
} // namespace la::pdf
