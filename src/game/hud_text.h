#pragma once
#include "raylib.h"
#include <algorithm>
#include <string>
#include <vector>

// Never split a UTF-8 sequence or shrink a label below the readable HUD size.
inline std::string hudFitText(Font font,const std::string& text,float size,float width) {
    if(MeasureTextEx(font,text.c_str(),size,0).x<=width) return text;
    const std::string suffix="...";
    std::string result;
    for(size_t offset=0;offset<text.size();) {
        int bytes=0;GetCodepointNext(text.c_str()+offset,&bytes);
        if(bytes<=0) break;
        auto next=result+text.substr(offset,(size_t)bytes);
        if(MeasureTextEx(font,(next+suffix).c_str(),size,0).x>width) break;
        result=std::move(next);offset+=(size_t)bytes;
    }
    return result+suffix;
}

inline std::vector<std::string> hudWrapText(Font font,const std::string& text,float size,float width) {
    std::vector<std::string> lines;
    std::string line;
    for(size_t offset=0;offset<text.size();) {
        int bytes=0;GetCodepointNext(text.c_str()+offset,&bytes);
        if(bytes<=0) break;
        std::string next=text.substr(offset,(size_t)bytes);offset+=(size_t)bytes;
        if(next=="\n") { lines.push_back(line);line.clear();continue; }
        if(!line.empty() && MeasureTextEx(font,(line+next).c_str(),size,0).x>width) {
            // Keep space-delimited words intact; CJK and overlong tokens
            // still fall back to UTF-8 codepoint boundaries.
            const auto space=line.find_last_of(" \t");
            if(next==" " || next=="\t" || space==std::string::npos) {
                lines.push_back(line);line.clear();
            } else {
                lines.push_back(line.substr(0,space));line=line.substr(space+1);
                if(!line.empty() && MeasureTextEx(font,(line+next).c_str(),size,0).x>width) {
                    lines.push_back(line);line.clear();
                }
            }
        }
        if(line.empty() && (next==" " || next=="\t")) continue;
        line+=next;
    }
    if(!line.empty()) lines.push_back(line);
    return lines;
}
