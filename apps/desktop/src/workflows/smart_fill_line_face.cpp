#include "smart_fill_line_face.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace pikdraw::desktop::detail {
namespace {
using P = geometry::Point2D;
constexpr double eps = 1e-8;
constexpr double hit_eps = 0.001;
constexpr std::size_t max_segments = 512;
struct Segment { P a, b; std::vector<double> cuts{0.0, 1.0}; };
struct Edge { std::size_t from, to, twin, next{}; bool visited{false}; };
double cross(P a, P b) { return a.x * b.y - a.y * b.x; }
double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
LineFaceResult fail(CutGeometryStatus status, const char* message) { return {status, {}, message}; }
}
LineFaceResult resolve_line_face(const std::vector<geometry::Path2D>& paths, P point,
                                 std::stop_token stop) {
    std::vector<Segment> segments;
    for (const auto& path : paths) {
        P first{}, previous{};
        bool active = false;
        for (const auto& e : path.elements()) {
            if (e.verb == geometry::PathVerb::MoveTo) { first = previous = e.pt; active = true; }
            else if (e.verb == geometry::PathVerb::LineTo && active) {
                if (previous.distance_to(e.pt) > eps) segments.push_back({previous, e.pt});
                previous = e.pt;
            } else if (e.verb == geometry::PathVerb::Close && active) {
                if (previous.distance_to(first) > eps) segments.push_back({previous, first});
                active = false;
            } else return fail(CutGeometryStatus::Unsupported,
                "Open-path regions currently require straight segments");
            if (segments.size() > max_segments)
                return fail(CutGeometryStatus::BudgetExceeded, "Open-path region exceeds 512 segments");
        }
    }
    if (segments.empty()) return fail(CutGeometryStatus::NoChange, "No usable boundary segments");
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (stop.stop_requested()) return fail(CutGeometryStatus::Cancelled, "Smart Fill was cancelled");
        for (std::size_t j = i + 1; j < segments.size(); ++j) {
            auto& a = segments[i]; auto& b = segments[j];
            const P r = a.b - a.a, s = b.b - b.a, d = b.a - a.a;
            const double det = cross(r, s), scale = std::sqrt(dot(r,r) * dot(s,s));
            if (std::abs(det) <= eps * scale) {
                if (std::abs(cross(d, r)) <= eps * std::sqrt(dot(r,r))) {
                    const double rr = dot(r,r);
                    const double t0 = dot(d,r)/rr, t1 = dot(b.b-a.a,r)/rr;
                    if (std::min(1.0,std::max(t0,t1)) - std::max(0.0,std::min(t0,t1)) > eps)
                        return fail(CutGeometryStatus::Ambiguous, "Overlapping boundary segments are ambiguous");
                }
                continue;
            }
            const double t = cross(d,s)/det, u = cross(d,r)/det;
            if (t >= -eps && t <= 1.0+eps && u >= -eps && u <= 1.0+eps) {
                a.cuts.push_back(std::clamp(t,0.0,1.0));
                b.cuts.push_back(std::clamp(u,0.0,1.0));
            }
        }
    }
    std::vector<P> vertices;
    std::vector<Edge> edges;
    std::vector<std::vector<std::size_t>> outgoing;
    auto vertex = [&](P p) {
        for (std::size_t i=0;i<vertices.size();++i) if (vertices[i].distance_to(p) <= eps) return i;
        vertices.push_back(p); outgoing.emplace_back(); return vertices.size()-1;
    };
    for (auto& segment : segments) {
        std::sort(segment.cuts.begin(), segment.cuts.end());
        segment.cuts.erase(std::unique(segment.cuts.begin(), segment.cuts.end(),
            [](double a,double b){ return std::abs(a-b) <= eps; }),segment.cuts.end());
        for (std::size_t k=1;k<segment.cuts.size();++k) {
            P a=segment.a+(segment.b-segment.a)*segment.cuts[k-1];
            P b=segment.a+(segment.b-segment.a)*segment.cuts[k];
            if (a.distance_to(b) <= eps) continue;
            if (edges.size() >= 8192) return fail(CutGeometryStatus::BudgetExceeded, "Open-path region has too many intersections");
            const auto v=vertex(a), w=vertex(b), n=edges.size();
            edges.push_back({v,w,n+1}); edges.push_back({w,v,n});
            outgoing[v].push_back(n); outgoing[w].push_back(n+1);
        }
    }
    for (auto& list : outgoing) std::sort(list.begin(),list.end(),[&](auto a,auto b){
        const P da=vertices[edges[a].to]-vertices[edges[a].from];
        const P db=vertices[edges[b].to]-vertices[edges[b].from];
        return std::atan2(da.y,da.x)<std::atan2(db.y,db.x);
    });
    for (auto& edge : edges) {
        const auto& list = outgoing[edge.to];
        const auto it=std::find(list.begin(),list.end(),edge.twin);
        if (it==list.end()) return fail(CutGeometryStatus::Invalid, "Invalid boundary graph");
        const auto index=static_cast<std::size_t>(it-list.begin());
        edge.next=list[(index+list.size()-1)%list.size()];
    }
    geometry::Path2D selected;
    std::vector<P> selected_polygon;
    std::size_t matches=0;
    for (std::size_t start=0;start<edges.size();++start) {
        if (edges[start].visited) continue;
        if (stop.stop_requested()) return fail(CutGeometryStatus::Cancelled, "Smart Fill was cancelled");
        std::vector<P> polygon;
        std::size_t e=start;
        do {
            if (edges[e].visited) return fail(CutGeometryStatus::Ambiguous, "Boundary graph is ambiguous");
            edges[e].visited=true; polygon.push_back(vertices[edges[e].from]); e=edges[e].next;
            if (polygon.size()>edges.size()) return fail(CutGeometryStatus::Invalid, "Invalid boundary cycle");
        } while (e!=start);
        if (polygon.size()<3) continue;
        double area=0.0; bool inside=false;
        for (std::size_t i=0;i<polygon.size();++i) {
            const P a=polygon[i], b=polygon[(i+1)%polygon.size()], d=b-a;
            area += cross(a-point,b-point);
            const double len=dot(d,d);
            if (len>0) {
                const double t=std::clamp(dot(point-a,d)/len,0.0,1.0);
                if ((a+d*t).distance_to(point)<=hit_eps)
                    return fail(CutGeometryStatus::Ambiguous, "Click away from the boundary");
            }
            if ((a.y>point.y)!=(b.y>point.y) &&
                point.x < (b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
        }
        if (area <= eps || !inside) continue;
        ++matches;
        for (std::size_t i=0;i<polygon.size();++i)
            for (std::size_t j=i+1;j<polygon.size();++j)
                if (polygon[i].distance_to(polygon[j]) <= eps)
                    return fail(CutGeometryStatus::Ambiguous, "Boundary contains a repeated junction");
        selected_polygon = polygon;
        selected.move_to(polygon.front());
        for (std::size_t i=1;i<polygon.size();++i) selected.line_to(polygon[i]);
        selected.close();
    }
    if (matches==0) return fail(CutGeometryStatus::NoChange, "Click is outside an enclosed region");
    if (matches>1) return fail(CutGeometryStatus::Ambiguous, "Nested regions require a hole-aware arrangement");
    // A disconnected contour inside the chosen cycle could form a hole. Do not
    // silently fill it until hole-aware arrangements are supported.
    for (std::size_t k=0;k<edges.size();k+=2) {
        const P midpoint=(vertices[edges[k].from]+vertices[edges[k].to])*0.5;
        bool inside=false, on_boundary=false;
        for (std::size_t i=0;i<selected_polygon.size();++i) {
            const P a=selected_polygon[i], b=selected_polygon[(i+1)%selected_polygon.size()], d=b-a;
            const double length=dot(d,d);
            if (length>0) {
                const double t=std::clamp(dot(midpoint-a,d)/length,0.0,1.0);
                if ((a+d*t).distance_to(midpoint)<=eps) { on_boundary=true; break; }
            }
            if ((a.y>midpoint.y)!=(b.y>midpoint.y) &&
                midpoint.x<(b.x-a.x)*(midpoint.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
        }
        if (inside && !on_boundary)
            return fail(CutGeometryStatus::Ambiguous, "Interior boundaries require a hole-aware arrangement");
    }
    return {CutGeometryStatus::Success,std::move(selected),"Enclosed region resolved"};
}
}
