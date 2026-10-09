(function (root) {
  'use strict';

  function sectionView(state, enabled, online) {
    const stage = state.stage;
    const moving = stage === 'SCANNING' || stage === 'MOVING_SECTION';
    const preScan = stage === 'PRE_SCAN';
    return {
      action: moving ? 'STOP' : preScan ? 'RUN' : 'GO',
      disabled: !online || (!moving && (preScan
        ? !enabled || !state.stationary
        : stage !== 'READY_FOR_SECTION' || !state.count || !state.stationary)),
      showConfiguration: !preScan && stage !== 'SCANNING',
      showNewScan: !preScan && !moving,
      showDirection: preScan,
      moving,
    };
  }

  function sectionColor(width, min, max) {
    const fraction = max > min ? Math.max(0, Math.min(1, (width - min) / (max - min))) : 0;
    return `hsl(${Math.round(270 * fraction)}, 82%, 48%)`;
  }

  class SectionControlUI {
    constructor(doc, send) {
      this.doc = doc;
      this.send = send;
      this.state = { stage: 'PRE_SCAN', state: 'STOPPED', stationary: true, count: 0,
        spanCm: 0, widthsCm: [], scanDirection: 'cw', completed: 0 };
      this.enabled = false;
      this.online = false;
      this.pending = false;
      this.requestId = 0;
      this.countDirty = false;
      this.direction = 'cw';
      this.stripKey = '';
      this.error = '';
      this.timer = null;
      const ids = ['state', 'span', 'total', 'next', 'next-label', 'summary', 'scan-setup',
        'scan-lb', 'scan-rb', 'scan-note', 'config', 'count', 'count-helper', 'visuals',
        'overview', 'details', 'overview-title', 'span-end', 'progress', 'return-note',
        'action', 'action-label', 'action-icon', 'new-scan', 'message'];
      this.el = {};
      ids.forEach(id => { this.el[id] = doc.getElementById(`section-${id}`); });
      this.el['scan-lb'].addEventListener('click', () => this.selectDirection('ccw'));
      this.el['scan-rb'].addEventListener('click', () => this.selectDirection('cw'));
      this.el.action.addEventListener('click', () => this.primaryAction());
      this.el['new-scan'].addEventListener('click', () => this.command({ cmd: 'section_new_scan' }));
      this.el.count.addEventListener('input', () => {
        this.countDirty = true;
        this.error = '';
        this.render();
        clearTimeout(this.timer);
        this.timer = setTimeout(() => this.configure(), 300);
      });
      this.render();
    }

    setOnline(online) { this.online = online; this.render(); }

    update(data) {
      if (typeof data.sectionError === 'string') this.error = data.sectionError;
      if (data.state !== undefined) this.enabled = data.state === 'on';
      if (data.section) {
        const previousStage = this.state.stage;
        this.state = data.section;
        this.online = true;
        if (previousStage !== this.state.stage && this.state.stage === 'PRE_SCAN') {
          this.countDirty = false;
          this.el.count.value = '';
          this.error = '';
        }
        if (!this.countDirty && this.doc.activeElement !== this.el.count) {
          this.el.count.value = this.state.count ? String(this.state.count) : '';
        }
      }
      this.render();
    }

    selectDirection(direction) {
      if (!this.online || this.pending || this.state.stage !== 'PRE_SCAN') return;
      this.direction = direction;
      this.render();
    }

    async configure() {
      if (!this.online || this.state.locked || this.pending) return;
      const value = this.el.count.value.trim();
      const count = value === '' ? NaN : Number(value);
      await this.command({ cmd: 'section_configure', count });
    }

    async primaryAction() {
      const view = sectionView(this.state, this.enabled, this.online);
      if (view.disabled || (this.pending && !view.moving) || (this.countDirty && !view.moving)) return;
      const cmd = view.action === 'STOP' ? { cmd: 'section_stop' }
        : view.action === 'RUN' ? { cmd: 'section_scan', direction: this.direction }
        : { cmd: 'section_go' };
      await this.command(cmd);
    }

    async command(cmd) {
      if (!this.online || (this.pending && cmd.cmd !== 'section_stop')) return;
      const requestId = ++this.requestId;
      this.pending = true;
      this.error = '';
      this.render();
      try {
        const data = await this.send(cmd);
        if (requestId !== this.requestId) return;
        if (cmd.cmd === 'section_configure') this.countDirty = false;
        if (data) this.update(data);
      } catch (error) {
        if (requestId === this.requestId) this.error = error.message;
      } finally {
        if (requestId === this.requestId) this.pending = false;
        this.render();
      }
    }

    renderStrips() {
      const s = this.state;
      const widths = s.widthsCm || [];
      const key = JSON.stringify([widths, s.completed, s.nextSection, s.scanDirection]);
      if (key === this.stripKey) return;
      const changedPlan = this.planKey !== JSON.stringify(widths);
      this.planKey = JSON.stringify(widths);
      this.stripKey = key;
      const oldScroll = this.el.details.scrollLeft;
      this.el.overview.replaceChildren();
      this.el.details.replaceChildren();
      if (!widths.length) return;
      const min = Math.min(...widths), max = Math.max(...widths);
      widths.forEach((width, index) => {
        const number = index + 1;
        const completed = s.scanDirection === 'cw' ? number > widths.length - s.completed : number <= s.completed;
        const next = number === s.nextSection;
        const color = sectionColor(width, min, max);
        const title = `Section ${number}: ${(width / 100).toFixed(2)} m${completed ? ', completed' : next ? ', next' : ''}`;
        const segment = this.doc.createElement('span');
        segment.style.flexGrow = String(width);
        segment.style.backgroundColor = color;
        segment.className = 'section-segment';
        segment.classList.toggle('is-complete', completed);
        segment.classList.toggle('is-next', next);
        segment.setAttribute('title', title);
        this.el.overview.appendChild(segment);
        const detail = this.doc.createElement('div');
        detail.className = 'section-detail';
        detail.textContent = `S${number}\n${(width / 100).toFixed(2)} m`;
        detail.style.flexBasis = `${Math.round(76 * width / min)}px`;
        detail.style.backgroundColor = color;
        detail.classList.toggle('is-complete', completed);
        detail.classList.toggle('is-next', next);
        detail.setAttribute('aria-label', title);
        if (next) detail.setAttribute('aria-current', 'step');
        this.el.details.appendChild(detail);
      });
      if (changedPlan && s.scanDirection === 'cw') this.el.details.scrollLeft = this.el.details.scrollWidth;
      else this.el.details.scrollLeft = oldScroll;
    }

    render() {
      const s = this.state, el = this.el;
      const view = sectionView(s, this.enabled, this.online);
      el.state.textContent = s.state;
      el.state.classList.toggle('status-on', s.state !== 'STOPPED');
      el.state.classList.toggle('status-off', s.state === 'STOPPED');
      el['scan-setup'].hidden = !view.showDirection;
      el.config.hidden = !view.showConfiguration;
      el.summary.hidden = s.stage === 'PRE_SCAN';
      el.visuals.hidden = !s.count;
      el['new-scan'].hidden = !view.showNewScan;
      el['new-scan'].disabled = !this.online || this.pending || !s.stationary;
      el.count.disabled = !this.online || this.pending || s.locked || !s.stationary || s.maxCount < 6;
      el.count.max = String(Math.max(6, s.maxCount || 60));
      el['count-helper'].textContent = !s.stationary ? 'Waiting for the traveller to stop…'
        : s.maxCount < 6 ? 'Span too short for 6 sections of at least 10 cm. Start a new scan.'
        : `Maximum valid sections for this span: ${s.maxCount}. Minimum width: 10 cm.`;
      el.span.textContent = `${((s.spanCm || 0) / 100).toFixed(2)} m`;
      el.total.textContent = s.count ? String(s.count) : '—';
      el['next-label'].textContent = s.stage === 'SCANNING' ? 'Scan direction' : 'Next distance';
      el.next.textContent = s.stage === 'SCANNING' ? (s.scanDirection === 'cw' ? 'RB →' : '← LB')
        : s.nextSection ? `${((s.remainingCm || 0) / 100).toFixed(2)} m` : '—';
      el['span-end'].textContent = `${((s.spanCm || 0) / 100).toFixed(2)} m`;
      el['overview-title'].textContent = `Section overview (${s.count || 0} sections)`;
      el['return-note'].textContent = `Gauging toward ${s.scanDirection === 'cw' ? 'LB ←' : 'RB →'} · Colours show section width`;
      el.progress.textContent = s.stage === 'FINISHED'
        ? 'All sections completed. Start a new scan to run again.'
        : s.count ? `${s.completed} / ${s.count} completed${s.lastCompletedSection ? ` · Last: S${s.lastCompletedSection}` : ''} · ${view.moving ? 'Moving' : 'Next'}: S${s.nextSection}`
        : s.stage === 'SCANNING' ? 'Scanning the span. Press STOP at the far bank.'
        : view.showConfiguration ? 'Enter the number of sections to generate your return journey.'
        : 'Select a scan direction, then press RUN.';
      el.action.disabled = view.disabled || (this.pending && !view.moving) || (this.countDirty && !view.moving);
      el['action-label'].textContent = s.stage === 'FINISHED' ? 'COMPLETE' : view.action;
      el['action-icon'].textContent = view.action === 'STOP' ? '■' : s.stage === 'FINISHED' ? '✓' : '▷';
      el.action.classList.toggle('action-off', view.moving);
      el.action.classList.toggle('action-on', !view.moving);
      el['scan-lb'].classList.toggle('selected', this.direction === 'ccw');
      el['scan-rb'].classList.toggle('selected', this.direction === 'cw');
      ['scan-lb', 'scan-rb'].forEach(id => {
        el[id].disabled = !this.online || this.pending;
        el[id].setAttribute('aria-pressed', String(id === 'scan-lb' ? this.direction === 'ccw' : this.direction === 'cw'));
      });
      el.message.textContent = this.error || s.error || '';
      this.renderStrips();
    }
  }

  if (typeof module !== 'undefined' && module.exports) module.exports = { sectionView, sectionColor, SectionControlUI };
  else root.SectionControlUI = SectionControlUI;
})(typeof window === 'undefined' ? globalThis : window);
