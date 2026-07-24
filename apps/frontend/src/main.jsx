import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

const NAV_ITEMS = ['Overview', 'Machines', 'Production', 'Downtime', 'OEE', 'Reports'];
const FILTERS = ['All', 'Running', 'Attention', 'Offline'];
const OFFLINE_AFTER_MS = 90_000;

const round = (value) => Number(value || 0).toFixed(1);
const statusSlug = (status = '') => status.toLowerCase().replace(/[^a-z0-9]+/g, '-');
const statusClass = (status = '') => `status status-${statusSlug(status)}`;
const isOffline = (machine, now = Date.now()) => now - new Date(machine.recordedAt).getTime() > OFFLINE_AFTER_MS;

function mergeMachine(current, incoming) {
  const index = current.findIndex((machine) => machine.machineId === incoming.machineId);
  if (index === -1) return [...current, incoming].sort((a, b) => a.machineId.localeCompare(b.machineId));
  const next = [...current];
  next[index] = incoming;
  return next;
}

function Gauge({ label, value, target = 85 }) {
  const safe = Math.max(0, Math.min(100, Number(value) || 0));
  return (
    <article className="oee-gauge-card">
      <div className="gauge" style={{ '--gauge-value': `${safe * 3.6}deg` }}>
        <div><strong>{round(safe)}%</strong><span>{label}</span></div>
      </div>
      <small>Target {target}%</small>
    </article>
  );
}

function Dashboard() {
  const [machines, setMachines] = useState([]);
  const [oee, setOee] = useState(null);
  const [error, setError] = useState('');
  const [activeView, setActiveView] = useState('Overview');
  const [lastUpdated, setLastUpdated] = useState(null);
  const [loading, setLoading] = useState(true);
  const [streamState, setStreamState] = useState('connecting');
  const [filter, setFilter] = useState('All');
  const [search, setSearch] = useState('');
  const [clock, setClock] = useState(Date.now());
  const [windowHours, setWindowHours] = useState(8);

  async function refreshMachines() {
    const response = await fetch('/api/v1/machines/latest');
    if (!response.ok) throw new Error(`Machine API error ${response.status}`);
    setMachines(await response.json());
  }

  async function refreshOee(hours = windowHours) {
    const response = await fetch(`/api/v1/oee?windowHours=${hours}`);
    if (!response.ok) throw new Error(`OEE API error ${response.status}`);
    setOee(await response.json());
  }

  async function refresh() {
    try {
      await Promise.all([refreshMachines(), refreshOee()]);
      setLastUpdated(new Date());
      setError('');
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    refresh();
    const fallbackTimer = setInterval(refresh, 60_000);
    const clockTimer = setInterval(() => setClock(Date.now()), 10_000);
    const events = new EventSource('/api/v1/events');

    events.addEventListener('connected', () => setStreamState('live'));
    events.addEventListener('heartbeat', () => setStreamState('live'));
    events.addEventListener('telemetry', (event) => {
      try {
        setMachines((current) => mergeMachine(current, JSON.parse(event.data)));
        setLastUpdated(new Date());
        setStreamState('live');
        refreshOee();
      } catch {
        setError('Received invalid live telemetry');
      }
    });
    events.onerror = () => setStreamState('reconnecting');

    return () => {
      clearInterval(fallbackTimer);
      clearInterval(clockTimer);
      events.close();
    };
  }, []);

  useEffect(() => {
    refreshOee(windowHours).catch((err) => setError(err.message));
  }, [windowHours]);

  const summary = useMemo(() => {
    const online = machines.filter((machine) => !isOffline(machine, clock));
    const running = online.filter((machine) => machine.status?.toUpperCase() === 'RUNNING').length;
    return {
      total: machines.length,
      online: online.length,
      running,
      attention: online.length - running,
      offline: machines.length - online.length,
      production: machines.reduce((sum, machine) => sum + Number(machine.productionCount || 0), 0),
      rejects: machines.reduce((sum, machine) => sum + Number(machine.rejectCount || 0), 0),
    };
  }, [machines, clock]);

  const visibleMachines = useMemo(() => {
    const query = search.trim().toLowerCase();
    return machines.filter((machine) => {
      const offline = isOffline(machine, clock);
      const status = machine.status?.toUpperCase();
      return (filter === 'All' || (filter === 'Running' && !offline && status === 'RUNNING') || (filter === 'Attention' && !offline && status !== 'RUNNING') || (filter === 'Offline' && offline))
        && (!query || machine.machineId.toLowerCase().includes(query) || String(machine.lossCode || '').toLowerCase().includes(query));
    });
  }, [machines, filter, search, clock]);

  const oeeSummary = oee?.summary || {};

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand"><div className="brand-mark">A</div><div><strong>AFMS</strong><span>Factory Intelligence</span></div></div>
        <nav>{NAV_ITEMS.map((item) => <button className={activeView === item ? 'nav-item active' : 'nav-item'} key={item} onClick={() => setActiveView(item)}><span className="nav-dot" />{item}</button>)}</nav>
        <div className={`sidebar-footer stream-${streamState}`}><span className="system-dot" /> {streamState === 'live' ? 'Live connection' : 'Reconnecting'}</div>
      </aside>

      <main className="content">
        <header className="topbar">
          <div><p className="eyebrow">AFMS v4.0 · Commit 0026</p><h1>{activeView}</h1><p className="subtitle">Real-time production intelligence and OEE visibility</p></div>
          <div className="topbar-actions"><div className="updated">Last update<br /><strong>{lastUpdated ? lastUpdated.toLocaleTimeString() : 'Waiting...'}</strong></div><button className="refresh-button" onClick={refresh} disabled={loading}>{loading ? 'Loading...' : 'Sync now'}</button></div>
        </header>

        {error && <div className="alert"><strong>Connection warning:</strong> {error}</div>}

        {activeView === 'OEE' ? (
          <>
            <section className="oee-toolbar"><div><p className="eyebrow">Current production window</p><h2>Overall Equipment Effectiveness</h2></div><label>Window<select value={windowHours} onChange={(event) => setWindowHours(Number(event.target.value))}><option value="1">1 hour</option><option value="8">8 hours</option><option value="12">12 hours</option><option value="24">24 hours</option><option value="168">7 days</option></select></label></section>
            <section className="oee-gauge-grid"><Gauge label="OEE" value={oeeSummary.oee} /><Gauge label="Availability" value={oeeSummary.availability} target={90} /><Gauge label="Performance" value={oeeSummary.performance} target={95} /><Gauge label="Quality" value={oeeSummary.quality} target={99} /></section>
            <section className="summary-grid oee-summary-grid"><article className="summary-card"><span>Total parts</span><strong>{oeeSummary.totalParts || 0}</strong><small>Within selected window</small></article><article className="summary-card accent-running"><span>Good parts</span><strong>{oeeSummary.goodParts || 0}</strong><small>Accepted production</small></article><article className="summary-card accent-attention"><span>Reject parts</span><strong>{oeeSummary.rejectParts || 0}</strong><small>Quality losses</small></article><article className="summary-card"><span>Machines measured</span><strong>{oeeSummary.machines || 0}</strong><small>Telemetry-based calculation</small></article></section>
            <section className="section-heading"><div><p className="eyebrow">Machine comparison</p><h2>OEE by machine</h2></div><span className="calculation-note">Derived from telemetry</span></section>
            <section className="oee-table-wrap"><table className="oee-table"><thead><tr><th>Machine</th><th>Availability</th><th>Performance</th><th>Quality</th><th>OEE</th><th>Good / Total</th></tr></thead><tbody>{(oee?.machines || []).map((machine) => <tr key={machine.machineId}><td><strong>{machine.machineId}</strong></td><td>{round(machine.availability)}%</td><td>{round(machine.performance)}%</td><td>{round(machine.quality)}%</td><td><span className={`oee-pill ${machine.oee >= 85 ? 'good' : machine.oee >= 60 ? 'warning' : 'bad'}`}>{round(machine.oee)}%</span></td><td>{machine.goodParts} / {machine.totalParts}</td></tr>)}</tbody></table>{oee?.machines?.length === 0 && <div className="empty-table">More than one telemetry sample per machine is needed to calculate OEE.</div>}</section>
            <p className="oee-disclaimer">Calculation uses running telemetry intervals, cumulative production and reject counters, and reported cycle time. Configure machine-specific ideal cycle times in a later setup milestone.</p>
          </>
        ) : (
          <>
            <section className="summary-grid summary-grid-five"><article className="summary-card"><span>Total machines</span><strong>{summary.total}</strong><small>{summary.online} currently online</small></article><article className="summary-card accent-running"><span>Running</span><strong>{summary.running}</strong><small>Producing now</small></article><article className="summary-card accent-attention"><span>Needs attention</span><strong>{summary.attention}</strong><small>Idle, setup or stopped</small></article><article className="summary-card accent-offline"><span>Offline</span><strong>{summary.offline}</strong><small>No update for 90 seconds</small></article><article className="summary-card"><span>Total production</span><strong>{summary.production}</strong><small>{summary.rejects} rejects recorded</small></article></section>
            <section className="section-heading"><div><p className="eyebrow">Live floor status</p><h2>Machine dashboard</h2></div><span className={`live-badge ${streamState !== 'live' ? 'reconnecting' : ''}`}><i /> {streamState === 'live' ? 'Streaming live' : 'Reconnecting'}</span></section>
            <section className="machine-toolbar"><div className="filter-group">{FILTERS.map((item) => <button key={item} className={filter === item ? 'filter active' : 'filter'} onClick={() => setFilter(item)}>{item}{item === 'Offline' && summary.offline > 0 ? ` (${summary.offline})` : ''}</button>)}</div><input aria-label="Search machines" placeholder="Search machine or loss code" value={search} onChange={(event) => setSearch(event.target.value)} /></section>
            <section className="machine-grid">{!loading && machines.length === 0 && <article className="empty-state"><div className="empty-icon">⌁</div><h2>No telemetry yet</h2><p>Waiting for an ESP8266 machine controller.</p></article>}{visibleMachines.map((machine) => { const offline = isOffline(machine, clock); const displayedStatus = offline ? 'OFFLINE' : machine.status; const goodCount = Math.max(0, Number(machine.productionCount || 0) - Number(machine.rejectCount || 0)); const quality = Number(machine.productionCount) > 0 ? ((goodCount / Number(machine.productionCount)) * 100).toFixed(1) : '100.0'; return <article className={`machine-card machine-${statusSlug(displayedStatus)}`} key={machine.machineId}><div className="machine-header"><div><p className="machine-label">Machine</p><h3>{machine.machineId}</h3></div><span className={statusClass(displayedStatus)}><i />{displayedStatus}</span></div><div className="metrics-grid"><div><span>Production</span><strong>{machine.productionCount}</strong></div><div><span>Good parts</span><strong>{goodCount}</strong></div><div><span>Rejects</span><strong>{machine.rejectCount}</strong></div><div><span>Quality</span><strong>{quality}%</strong></div><div><span>Cycle time</span><strong>{(Number(machine.cycleTimeMs || 0) / 1000).toFixed(1)} s</strong></div><div><span>Signal age</span><strong>{Math.max(0, Math.round((clock - new Date(machine.recordedAt).getTime()) / 1000))} s</strong></div></div><div className="machine-footer"><div><span>Current loss</span><strong>{machine.lossCode || 'No active loss'}</strong></div><time>Updated {new Date(machine.recordedAt).toLocaleString()}</time></div></article>; })}</section>
          </>
        )}
      </main>
    </div>
  );
}

createRoot(document.getElementById('root')).render(<Dashboard />);
