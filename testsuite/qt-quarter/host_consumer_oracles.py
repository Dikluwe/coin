"""Pixel and input oracles shared by the real host consumer fixture.

These checks consume observations; they never prepare a scene or issue input.
The retained-node profile's 20/30 pixel gates also apply to these consumers.
"""
import math
import os
import re


CONSUMERS = ('sketcher-datum', 'measure-distance', 'mesh-curvature',
             'fem-mesh', 'draft-dimension', 'partdesign-pad', 'transform-dragger',
             'bim-site', 'mesh-interactive')
LABEL_CONSUMERS = ('sketcher-datum', 'measure-distance', 'draft-dimension',
                   'mesh-curvature', 'bim-site')


class NativePublicationQuietWindow:
    """Observe publication progress, never pixels or requests for rendering."""
    def __init__(self, before, quiet_ms=600):
        self.before = (before['frames'], before['serial'])
        if any(type(value) is not int or value < 0 for value in self.before):
            raise RuntimeError('invalid initial native publication counters')
        self.quiet_ms = quiet_ms
        self.last = None
        self.quiet_started_ms = None

    def observe(self, state, now_ms):
        if state.get('fallback_reason') or state.get('opengl_viewport') is True:
            raise RuntimeError('native publication wait detected GL fallback')
        current = (state.get('frames'), state.get('serial'))
        valid = (state.get('viewer_alive') is True and type(state.get('status')) is int and
                 state['status'] == 0 and state.get('opengl_viewport') is False and
                 state.get('fallback_reason') == '' and
                 all(type(value) is int and value >= 0 for value in current))
        changed = current != self.last
        self.last = current
        progressed = valid and all(value > initial for value,initial in zip(current,self.before))
        if not progressed:
            self.quiet_started_ms = None
            return False
        if changed or self.quiet_started_ms is None:
            self.quiet_started_ms = now_ms
        return now_ms-self.quiet_started_ms >= self.quiet_ms


def loaded_coin_images(maps):
    images = {}
    for line in maps.splitlines():
        fields = line.split(None, 5)
        if len(fields) != 6:
            continue
        path = fields[5].removesuffix(' (deleted)')
        if not re.search(r'(?:^|/)libCoin\.so(?:\.[^/]*)?$', path):
            continue
        canonical = os.path.realpath(path)
        inode = int(fields[4])
        identity = (fields[3], inode) if inode else ('path', canonical)
        image = images.setdefault(identity, {'device': fields[3], 'inode': inode, 'paths': []})
        if canonical not in image['paths']:
            image['paths'].append(canonical)
    return list(images.values())


def require_single_coin(maps):
    images = loaded_coin_images(maps)
    if len(images) != 1:
        raise RuntimeError('host requires exactly one loaded libCoin image; found ' + str(len(images)))
    return images


def require_content(changed, glyphs=None):
    if changed < 20:
        raise RuntimeError('consumer content missing')
    if glyphs is not None and glyphs < 20:
        raise RuntimeError('consumer glyphs missing; background does not count')


def require_host_hover(changed, provided):
    if provided:
        require_content(changed)
    elif changed != 0:
        raise RuntimeError('consumer changed pixels for hover absent from its CoinGL host')


def require_host_selection(changed, provided):
    if provided:
        require_content(changed)
    elif changed != 0:
        raise RuntimeError('consumer changed pixels for selection absent from its CoinGL host')


def require_released_draggers(state):
    active = state.get('scene_active_dragger_count')
    if (any(state.get(key) is not True for key in ('viewer_alive', 'manager_present', 'action_present')) or
            type(active) is not int or active != 0 or state.get('grabber_present') is not False):
        raise RuntimeError('released consumer retained an active dragger or event grabber')


def require_mutation(changed):
    if changed < 30:
        raise RuntimeError('consumer update did not change pixels')


def require_cleanup(changed):
    if changed > 30:
        raise RuntimeError('consumer left stale pixels after removal')


def require_pointer_identity(actual, expected):
    if not expected or actual != expected:
        raise RuntimeError('native pointer did not target the consumer')


def canonical_topology_identity(document, identity):
    owner, subname = identity
    if not subname:
        return identity
    obj = document.getObject(owner)
    if obj is None:
        raise RuntimeError('topology selection owner is missing')
    resolved, new_name, old_name = obj.resolveSubElement(subname, True, 0)
    if resolved is None or not old_name:
        raise RuntimeError('topology selection cannot be resolved by the host')
    # append=True returns the old-style element together with its hierarchy.
    # Keep the original Body/Link owner; never guess by stripping mapped names.
    return owner, old_name


def require_drag(before, after, motion_events, button_events, callbacks=None):
    if (len(before) != 3 or len(after) != 3 or
            not all(math.isfinite(v) for v in (*before, *after))):
        raise RuntimeError('invalid drag placement')
    if before == after or motion_events < 1 or button_events < 2:
        raise RuntimeError('native drag did not update consumer placement')
    if callbacks is not None and any(callbacks.get(key, 0) < 1 for key in ('start', 'motion', 'finish')):
        raise RuntimeError('native drag callback lifecycle incomplete')


def require_idle(before, after):
    if before != after:
        raise RuntimeError('consumer caused continuous redraw')


def require_dpr(actual, requested):
    if actual not in (1.0, 2.0) or actual != requested:
        raise RuntimeError('consumer did not execute at the requested DPR')


def require_interactive_mesh(facets, limit, pressed, camera_changed,
                             active_pixels, released_pixels,
                             projected_samples, sample_hits):
    if not (facets > limit > 0) or pressed is not True or camera_changed is not True:
        raise RuntimeError('mesh was not sampled during actual active navigation above its limit')
    stride = facets // limit + 1
    samples = (facets + stride - 1) // stride
    # The finite Mesh::Feature provider uses SoFCIndexedFaceSet: each sampled
    # facet contributes its three vertices, each with the established 3px size.
    if not (20 <= active_pixels <= samples * 3 * 9 and released_pixels > active_pixels):
        raise RuntimeError('interactive mesh did not use its established vertex point sampler')
    if projected_samples <= 0 or projected_samples > samples * 3 or sample_hits != projected_samples:
        raise RuntimeError('interactive mesh omitted or misplaced the authored vertex samples')
