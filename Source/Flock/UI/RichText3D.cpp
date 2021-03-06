//
// Copyright (c) 2008-2017 Flock SDK developers & contributors. 
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../UI/RichText3D.h"
#include "../UI/RichBatchText.h"
#include "../UI/RichBatchImage.h"
#include "../Core/StringUtils.h"
#include "../UI/RichMarkupParser.h"
#include "../Engine/Engine.h"
#include "../Core/CoreEvents.h"

// Don't you dare trying constexpr ;).
static const char *s_ticker_types[] = { "None", "Horizontal", "Vertical", "Both", nullptr };
static const char *s_ticker_directions[] = { "Negative", "Positive", nullptr };
static const char *s_horizontal_alignments[] = { "Left", "Center", "Right", nullptr };

namespace FlockSDK {

extern const char *GEOMETRY_CATEGORY;

// Register object factory. Drawable must be registered first.
void RichText3D::RegisterObject(Context* context)
{
  context->RegisterFactory<RichText3D>(GEOMETRY_CATEGORY);

  FLOCKSDK_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, false, AM_FILE);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Text", GetText, SetText, String, String::EMPTY, AM_DEFAULT);
  FLOCKSDK_MIXED_ACCESSOR_ATTRIBUTE("Font", GetFontAttr, SetFontAttr, ResourceRef, ResourceRef(Font::GetTypeStatic()), AM_DEFAULT);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Font Size", GetFontSizeAttr, SetFontSizeAttr, int, 14, AM_DEFAULT);
  FLOCKSDK_MIXED_ACCESSOR_ATTRIBUTE("Color", GetTextColor, SetTextColor, Color, Color::WHITE, AM_DEFAULT);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Word Wrap", GetWrapping, SetWrapping, bool, true, AM_DEFAULT);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Single Line", GetSingleLine, SetSingleLine, bool, false, AM_DEFAULT);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Line Spacing", GetLineSpacing, SetLineSpacing, int, 0, AM_DEFAULT);
  FLOCKSDK_ENUM_ACCESSOR_ATTRIBUTE("Text Alignment", GetAlignment, SetAlignment, HorizontalAlignment, s_horizontal_alignments, HA_LEFT, AM_DEFAULT);
  FLOCKSDK_ENUM_ACCESSOR_ATTRIBUTE("Ticker Type", GetTickerType, SetTickerType, TickerType, s_ticker_types, TickerType_None, AM_DEFAULT);
  FLOCKSDK_ENUM_ACCESSOR_ATTRIBUTE("Ticker Direction", GetTickerDirection, SetTickerDirection, TickerDirection, s_ticker_directions, TickerDirection_Negative, AM_DEFAULT);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Ticker Speed", GetTickerSpeed, SetTickerSpeed, float, 120, AM_DEFAULT);
  FLOCKSDK_ACCESSOR_ATTRIBUTE("Can Be Occluded", IsOccludee, SetOccludee, bool, true, AM_DEFAULT);
  FLOCKSDK_COPY_BASE_ATTRIBUTES(RichWidget);
  FLOCKSDK_COPY_BASE_ATTRIBUTES(Drawable);
}

RichText3D::RichText3D(Context* context)
 : tickerType_(TickerType_None)
 , tickerDirection_(TickerDirection_Negative)
 , tickerSpeed_(60)
 , scrollOrigin_(0.0f, 0.0f)
 , singleLine_(false)
 , wrapping_(WRAP_WORD)
 , tickerPosition_(0.0f)
 , refreshCount_(0)
 , alignment_(HA_LEFT)
 , lineSpacing_(0)
 , RichWidget(context)
{
    defaultFontState_.color = Color(Color::WHITE);
    SetDefaultFont("fonts/SDK/Anonymous Pro.ttf", 32);

    // TODO: disable this handler when the component is disabled
    SubscribeToEvent(FlockSDK::E_UPDATE, FLOCKSDK_HANDLER(RichText3D, UpdateTickerAnimation));
}

RichText3D::~RichText3D() {}

void RichText3D::SetText(const String &text)
{
    text_ = text;
    SetFlags(WidgetFlags_All);
    ResetTicker();
}

const String &RichText3D::GetText() const
{
    return text_;
}

void RichText3D::SetTextColor(const Color &color)
{
    defaultFontState_.color = color;
    SetFlags(WidgetFlags_All);
}

void RichText3D::SetAlignment(HorizontalAlignment align)
{
    alignment_ = align;
    SetFlags(WidgetFlags_All);
}

void RichText3D::SetLineSpacing(int lineSpacing)
{
    lineSpacing_ = lineSpacing;
    SetFlags(WidgetFlags_All);
}

void RichText3D::SetTickerType(TickerType type)
{
    tickerType_ = type;
    ResetTicker();
}

void RichText3D::SetWrapping(bool wrapping) {
    wrapping_ = wrapping ? WRAP_WORD : WRAP_NONE;
    SetFlags(WidgetFlags_All);
}

TickerType RichText3D::GetTickerType() const
{
    return tickerType_;
}

void RichText3D::SetTickerDirection(TickerDirection direction)
{
    tickerDirection_ = direction;
    ResetTicker();
}

TickerDirection RichText3D::GetTickerDirection() const
{
    return tickerDirection_;
}

void RichText3D::SetTickerSpeed(float pixelspersecond)
{
    tickerSpeed_ = pixelspersecond;
}

float RichText3D::GetTickerSpeed() const
{
    return tickerSpeed_;
}

void RichText3D::SetSingleLine(bool singleLine)
{
    singleLine_ = singleLine;
    SetFlags(WidgetFlags_All);
}

void RichText3D::ResetTicker()
{
    scrollOrigin_ = Vector3::ZERO;

    if (tickerType_ == TickerType_Horizontal)
      scrollOrigin_ = Vector3(content_size_.x_ * (tickerDirection_ == TickerDirection_Negative ? 1.0f : -1.0f), 0.0f, 0.0f);
    else if (tickerType_ == TickerType_Vertical)
      scrollOrigin_ = Vector3(0.0f, content_size_.y_ * (tickerDirection_ == TickerDirection_Negative ? 1.0f : -1.0f), 0.0f);
    else
      SetDrawOrigin(scrollOrigin_);

    tickerPosition_ = 0.0f;
}

float RichText3D::GetTickerPosition() const
{
    return tickerPosition_;
}

void RichText3D::SetTickerPosition(float tickerPosition)
{
    // TODO: implement
}

void RichText3D::SetDefaultFont(const String &face, unsigned size)
{
    defaultFontState_.face = face;
    defaultFontState_.size = size;
    defaultFontState_.bold = false;
    defaultFontState_.italic = false;
    defaultFontState_.underlined = false;
    SetFlags(WidgetFlags_All);
}

ResourceRef RichText3D::GetFontAttr() const
{
    return ResourceRef(Font::GetTypeStatic(), defaultFontState_.face);
}

void RichText3D::SetFontAttr(const ResourceRef& value)
{
    defaultFontState_.face = value.name_;
    SetFlags(WidgetFlags_All);
}

int RichText3D::GetFontSizeAttr() const
{
    return defaultFontState_.size;
}

void RichText3D::SetFontSizeAttr(int size)
{
    defaultFontState_.size = size;
    SetFlags(WidgetFlags_All);
}

void RichText3D::ArrangeTextBlocks(Vector<TextBlock>& markup_blocks)
{
  TextLine line;
  if (!singleLine_)
  {
    Vector<TextLine> markupLines;
    // for every new line in a block, create a new TextLine
    for (auto i = markup_blocks.Begin(); i != markup_blocks.End(); ++i) {
      size_t posNewLine = 0;
      size_t posLast = 0;
      if ((posNewLine = i->text.Find("\n", posLast)) != String::NPOS) {
        while ((posNewLine = i->text.Find("\n", posLast)) != String::NPOS) {
          TextBlock block;
          block.text = i->text.Substring(posLast, posNewLine - posLast);
          block.font = i->font;
          line.blocks.Push(block);
          markupLines.Push(line);
          posLast = posNewLine + 1;
          line.blocks.Clear();
        }
        if (posLast < i->text.Length() - 1) {
          TextBlock block;
          block.text = i->text.Substring(posLast, i->text.Length() - posLast);
          block.font = i->font;
          line.blocks.Push(block);
        }
      } else {
        if (i->is_line_brake) {
          line.blocks.Push(*i);
          markupLines.Push(line);
          line.blocks.Clear();
        }
        line.blocks.Push(*i);
      }
    }

    // make sure there's at least one line with text
    if (!line.blocks.Empty())
      markupLines.Push(line);

    IntRect actual_clip_region = GetClipRegion();
    // do the word wrapping and layout positioning
    if (wrapping_ == WRAP_WORD && actual_clip_region.Width()) {
      int layout_width = actual_clip_region.Width();
      int layout_height = actual_clip_region.Height();
      int layout_x = actual_clip_region.left_;
      int layout_y = actual_clip_region.top_;
      int draw_offset_x = layout_x;
      int draw_offset_y = layout_y;
      FontState fontstate;

      for (auto it = markupLines.Begin(); it != markupLines.End(); ++it) {
        TextLine* line = &*it;
        TextLine new_line;
        new_line.height = 0;
        new_line.width = 0;
        new_line.offset_x = layout_x;
        new_line.offset_y = 0;
        new_line.align = alignment_;

        // reset the x offset of the current line
        draw_offset_x = layout_x;

        for (auto bit = line->blocks.Begin(); bit != line->blocks.End(); ++bit) {
          TextBlock new_block;
          new_block.font = (*bit).font;

          if (!bit->is_iconic) {
            size_t crpos;
            while ((crpos = bit->text.Find("\r")) != String::NPOS)
              bit->text.Erase(crpos, 1);
            
            auto SplitWords = [] (const String &str, Vector<String>& tokens, const String &delimiters = " ", bool trimEmpty = false) -> void {
              unsigned pos, lastPos = 0;
              while(true) {
                pos = str.FindFirstOf(delimiters, lastPos);
                if(pos == String::NPOS) {
                  pos = str.Length();
                  if(pos != lastPos || !trimEmpty)
                    tokens.Push(String(str.CString() + lastPos, pos - lastPos));
                  break;
                }
                else {
                  if(pos != lastPos)
                    tokens.Push(String(str.CString() + lastPos, pos - lastPos));
                  if (!trimEmpty)
                    tokens.Push(String(str.CString() + pos, 1));
                }
                lastPos = pos + 1;
              }
            };

            Vector<String> words;
            SplitWords(bit->text, words, " \t,.:;", false);
            if (words.Empty())
              words.Push(bit->text);

            bool new_line_space = false;

            // bold-italic flags are passed as third asset name parameter
            auto bi = String(bit->font.bold ? "b" : "") + String(bit->font.italic ? "i" : "");
            if (!bi.Empty())
              bi = String(".") + bi;

            // this block's text renderer
            String id;
            fontstate.face = bit->font.face;
            fontstate.size = bit->font.size;
            if (fontstate.face.Empty())
              fontstate.face = defaultFontState_.face;
            if (fontstate.size <= 0)
              fontstate.size = defaultFontState_.size;
            id.AppendWithFormat("%s.%d%s", fontstate.face.CString(), fontstate.size, bi.CString());
            RichWidgetText* text_renderer = CacheWidgetBatch<RichWidgetText>(id);
            text_renderer->SetFont(fontstate.face, fontstate.size);

            // for every word in this block do a check if there's enough space on the current line
            // Simple word wrap logic: if the space is enough, put the word on the current line, else go to the next line
            String the_word;
            for (auto wit = words.Begin(); wit != words.End(); ++wit) {
              the_word = *wit;
              Vector2 wordsize = text_renderer->CalculateTextExtents(the_word);
              new_line.height = Max<int>((int) wordsize.y_, new_line.height);
              bool needs_new_line = (draw_offset_x + wordsize.x_) > layout_width;
              bool is_wider_than_line = wordsize.x_ > layout_width;

              // Handle cases where this word can't be fit in a line
              while (is_wider_than_line && needs_new_line) {
                String fit_word = the_word;
                int width_remain = (int) wordsize.x_ - layout_width;
                assert(width_remain >= 0);
                // remove a char from this word until it can be fit in the current line
                Vector2 newwordsize;
                while (width_remain > 0 && !the_word.Empty()) {
                  fit_word.Erase(fit_word.Length() - 1);
                  newwordsize = text_renderer->CalculateTextExtents(fit_word);
                  width_remain = (int) newwordsize.x_ - layout_width;
                }

                // prevent endless loops
                if (fit_word.Empty())
                  break;

                // append the fitting part of the word in the current line
                // and create a new line
                new_block.text.Append(fit_word);
                new_line.blocks.Push(new_block);
                draw_offset_x += (int) newwordsize.x_;
                new_line.width = draw_offset_x;
                new_line.offset_y = draw_offset_y;
                lines_.Push(new_line);
                // create a new empty line
                new_line.blocks.Clear();
                new_block.text.Clear();
                draw_offset_x = layout_x;
                draw_offset_y += new_line.height;
                the_word = the_word.Substring(fit_word.Length());
                wordsize = text_renderer->CalculateTextExtents(the_word);
                draw_offset_x += (int) wordsize.x_;
                is_wider_than_line = wordsize.x_ > layout_width;

                if (!is_wider_than_line) {
                  needs_new_line = false;
                  // leftovers from the_word will be added to the line below
                  break;
                }
              }

              // carry the whole word on a new line
              if (needs_new_line) {
                new_line.width = draw_offset_x;
                new_line.offset_y = draw_offset_y;
                new_line.blocks.Push(new_block);
                lines_.Push(new_line);
                // create next empty line
                new_line.blocks.Clear();
                // add current text to a block
                new_block.text.Clear();
                draw_offset_x = layout_x;
                draw_offset_y += new_line.height;
                if (the_word == " " || the_word == "\t") {
                  // mark as space
                  new_line_space = true;
                  continue;
                }
              }

              if (new_line_space) {
                if (the_word != " " && the_word != "\t")
                  new_line_space = false;
                else
                  continue;
              }
              new_block.text.Append(the_word);
              draw_offset_x += (int) wordsize.x_;
            }

            new_line.blocks.Push(new_block);
          } else {
            // if the block is iconic (image, video, etc)
            new_block = (*bit); // copy the block
            if (new_block.is_visible) {
              RichWidgetImage* image_renderer = CacheWidgetBatch<RichWidgetImage>(new_block.text);
              image_renderer->SetImageSource(new_block.text);
              if (new_block.image_height == 0) {
                float aspect = image_renderer->GetImageAspect();

                // fit by height
                //new_block.image_height = clip_region_.height();
                //new_block.image_width = new_block.image_height * aspect;

                // fit by width
                new_block.image_width = (float) clip_region_.Width();
                new_block.image_height = new_block.image_width / aspect;
              }

              if (new_block.image_width == 0)
                new_block.image_width = (float) clip_region_.Width();
              
              new_line.blocks.Push(new_block);
              draw_offset_x += (int) new_block.image_width;
              new_line.height = Max((int) new_block.image_height, new_line.height);
            }
          }

          new_line.width = draw_offset_x;
          new_line.offset_y = draw_offset_y;
          draw_offset_y += new_line.height;
        }

        lines_.Push(new_line);
        new_line.blocks.Clear();
      }
    } else {
      // in case there's no word wrapping or the layout has no width
      lines_ = markupLines;
    }


  } else {
    // Single line...
    // replace /n/r with empty space
    for (auto i = markup_blocks.Begin(); i != markup_blocks.End(); i++) {
      size_t crpos;
      while ((crpos = (i->text).FindFirstOf("\n\r")) != String::NPOS)
        i->text.Replace(crpos, 1, " ");
      
      // TODO: single line doesn't get images when width or height = 0
      line.blocks.Push(*i);
    }
    lines_.Push(line);
  }
}

void RichText3D::DrawTextLines()
{
  // clear all quads
  Clear();

  RichWidgetText* text_renderer;
  RichWidgetImage* image_renderer;

  int xoffset = 0, yoffset = 0;

  for (auto lit = lines_.Begin(); lit != lines_.End(); ++lit) {
    // adjust the size and offset of every block in a line
    TextLine* l = &(*lit);

    switch (l->align) {
    default:
    case HA_LEFT:
      xoffset = l->offset_x;
      break;
    case HA_CENTER:
      xoffset = (clip_region_.Width() - l->width) / 2;
      break;
    case HA_RIGHT:
      xoffset = clip_region_.Width() - l->width;
    }

    FontState fontstate;

    int line_max_height = 0;
    for (auto bit = l->blocks.Begin(); bit != l->blocks.End(); ++bit) {
      if (bit->is_iconic) {
        image_renderer = CacheWidgetBatch<RichWidgetImage>(bit->text);
        //image_renderer->clearQuads();
        if (image_renderer->GetImageSource() != bit->text)
          image_renderer->SetImageSource(bit->text);

        if (bit->image_width == 0)
          bit->image_width = (float) clip_region_.Width();
        if (bit->image_height == 0) {
          // height should be the line max height
          bit->image_height = (float) line_max_height;
          // width should be auto calculated based on the texture size
          bit->image_width = bit->image_height * image_renderer->GetImageAspect();
        }

        image_renderer->AddImage(Vector3((float) xoffset, (float) yoffset, 0.0f), bit->image_width, bit->image_height);
        line_max_height = Max((int) bit->image_height, line_max_height);
        xoffset += (int) bit->image_width;
      } else {
        // bold-italic flags are passed as third asset name parameter
        fontstate.face = bit->font.face;
        fontstate.size = bit->font.size;
        if (fontstate.face.Empty())
          fontstate.face = defaultFontState_.face;
        if (fontstate.size <= 0)
          fontstate.size = defaultFontState_.size;
        String bi = String(bit->font.bold ? "b" : "") + String(bit->font.italic ? "i" : "");
        if (!bi.Empty())
          bi = String(".") + bi;
        String id = fontstate.face + "." + ToString("%d", bit->font.size) + bi;
        id.AppendWithFormat("%s.%d%s", fontstate.face.CString(), fontstate.size, bi.CString());
        text_renderer = CacheWidgetBatch<RichWidgetText>(id);
        text_renderer->SetFont(fontstate.face, fontstate.size);
        text_renderer->AddText(bit->text, Vector3((float) xoffset, (float) yoffset, 0.0f), bit->font.color);
        if (text_renderer->GetFontFace()) {
          line_max_height = Max<int>((int) text_renderer->GetRowHeight(), line_max_height);
          xoffset += (int) text_renderer->CalculateTextExtents(bit->text).x_;
        }
      }
    }
    yoffset += line_max_height + lineSpacing_;
    content_size_.x_ = Max<float>(content_size_.x_, xoffset);
    content_size_.y_ = (float) yoffset;
  }
}

void RichText3D::CompileTextLayout()
{
	lines_.Clear();
	content_size_ = Vector2::ZERO;

  Vector<TextBlock> markup_blocks;
  markup_blocks.Reserve(10);

  ParseRichTextHTML(text_, markup_blocks, defaultFontState_);  
  ArrangeTextBlocks(markup_blocks);
  DrawTextLines();
}

void RichText3D::UpdateTickerAnimation(FlockSDK::StringHash eventType, FlockSDK::VariantMap& eventData)
{
  using namespace RenderUpdate;
  float elapsed = eventData[P_TIMESTEP].GetFloat();

  if (!IsEnabled())
    return;

  if (IsFlagged(WidgetFlags_ContentChanged))
  {
    CompileTextLayout();
    ClearFlags(WidgetFlags_ContentChanged);
  }

  // Update ticker state
  if (tickerType_ != TickerType_None)
  {
    // ticker direction sign
    int vertical_dir = 0, horizontal_dir = 0;

    if (tickerType_ == TickerType_Horizontal)
      horizontal_dir = tickerDirection_ == TickerDirection_Negative ? -1 : 1;
    else if (tickerType_ == TickerType_Vertical)
      vertical_dir = tickerDirection_ == TickerDirection_Negative ? -1 : 1;

    // move the ticker, cap the time elapsed to 100ms to prevent long jumps
    float move_factor = tickerSpeed_ * (elapsed < 0.1f ? elapsed : 0.1f);
    scrollOrigin_.x_ += horizontal_dir * move_factor;
    scrollOrigin_.y_ += vertical_dir * move_factor;

    // move the content
    SetDrawOrigin(scrollOrigin_);

    // check if the text has scrolled out
    bool scrolled_out = false;
    float ticker_max;

    IntRect actual_clip_region = GetClipRegion();
    // detect ticker scrolling out of the clip region and update the ticker position
    if (tickerType_ == TickerType_Horizontal)
    {
      ticker_max = (horizontal_dir == -1 ? content_size_.x_ : content_size_.x_ + actual_clip_region.Width());
      scrolled_out = horizontal_dir * scrollOrigin_.x_ > ticker_max;
      tickerPosition_ = ticker_max != 0 ? ((float) horizontal_dir * scrollOrigin_.x_ / ticker_max) : 0;
    }
    else if (tickerType_ == TickerType_Vertical)
    {
      ticker_max = (vertical_dir == -1 ? content_size_.y_ : content_size_.y_ + actual_clip_region.Height());
      scrolled_out = vertical_dir * scrollOrigin_.y_ > ticker_max;
      tickerPosition_ = ticker_max != 0 ? ((float) vertical_dir * scrollOrigin_.y_ / ticker_max) : 0;
    }

    if (scrolled_out)
    {
      if (content_size_ != Vector2::ZERO)
        refreshCount_++;

      // TODO: send an event
      ResetTicker();
    }
  }
}

} // namespace FlockSDK
