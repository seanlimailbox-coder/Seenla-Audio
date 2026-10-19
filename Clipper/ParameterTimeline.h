#pragma once

// VST3 queues are time-ordered. Merge them without allocating on the audio
// thread or imposing a fixed event limit. Repeated offsets retain queue order.
// Read(queue, point, offset, value) returns false for a malformed point.
namespace OpenClipperTimeline
{
template <typename PointCount, typename Read, typename Apply, typename Render>
void process (int samples, int queues, PointCount pointCount, Read read, Apply apply, Render render)
{
    int position = 0;
    do
    {
        int next = samples;
        for (int queue = 0; queue < queues; ++queue)
        {
            const int count = pointCount (queue);
            int lo = 0, hi = count;
            while (lo < hi)
            {
                const int middle = lo + (hi - lo) / 2;
                int offset = 0; double value = 0;
                if (! read (queue, middle, offset, value)) { lo = middle + 1; continue; }
                if (offset < position && position != 0) lo = middle + 1; else hi = middle;
            }
            for (int point = lo; point < count; ++point)
            {
                int offset = 0; double value = 0;
                if (! read (queue, point, offset, value)) continue;
                if (samples == 0) { apply (queue, value); continue; }
                if (offset < 0) offset = 0;
                if (offset >= samples) offset = samples - 1;
                if (offset == position) apply (queue, value);
                else if (offset > position) { if (offset < next) next = offset; break; }
            }
        }
        if (next > position) render (position, next - position);
        position = next;
    } while (position < samples);
}
}
