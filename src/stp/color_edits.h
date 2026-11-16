#pragma once
#include "cad_document.h"
#include "part_placement.h"
#include <QColor>
#include <QMetaType>
#include <TopoDS_Face.hxx>
#include <map>
#include <tuple>

namespace Stp {
// Face numbers are one-based in the immutable definition's complete face map.
// The occurrence is essential: repeated instances share the same definition.
struct FaceColorKey {
    int occurrence = -1;
    int face = 0;
    bool operator<(const FaceColorKey& b) const { return std::tie(occurrence,face)<std::tie(b.occurrence,b.face); }
    bool operator==(const FaceColorKey& b) const { return occurrence==b.occurrence&&face==b.face; }
    bool operator!=(const FaceColorKey& b) const { return !(*this==b); }
};
using FaceColorEdits = std::map<FaceColorKey,QColor>;
using ColorFaceSelection = std::vector<FaceColorKey>;
// Keep the existing occurrence-map interface; faces form a higher-priority layer.
struct ColorEdits : std::map<int,QColor> {
    using Base = std::map<int,QColor>;
    using Base::Base;
    FaceColorEdits faces;
    void clear() { Base::clear();faces.clear(); }
    bool empty() const { return Base::empty()&&faces.empty(); }
    bool operator==(const ColorEdits& b) const { return static_cast<const Base&>(*this)==static_cast<const Base&>(b)&&faces==b.faces; }
    bool operator!=(const ColorEdits& b) const { return !(*this==b); }
};
void validateColorEdits(const CadDocument&, const ColorEdits&);
QColor occurrenceBaseColor(const CadDocument&, int occurrence);
TopoDS_Face colorFace(const CadDocument&, const FaceColorKey&);
int colorFaceIndex(const CadDocument&, int occurrence, const TopoDS_Shape& canonicalFace);
QString colorNameGroup(const CadDocument&, int occurrence);
// A new complete color state: every leaf receives its name-group color; no face overrides.
// Uses applied placements, never visibility, explosion, camera or grab offsets.
ColorEdits randomColorsByName(const CadDocument&, const PlacementEdits&, quint32 seed,
                             std::atomic_bool* cancel = nullptr);
}
Q_DECLARE_METATYPE(Stp::FaceColorKey)
Q_DECLARE_METATYPE(Stp::ColorFaceSelection)
