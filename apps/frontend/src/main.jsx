import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

const NAV_ITEMS = ['Overview', 'Machines', 'Production', 'Downtime', 'OEE', 'Reports'];
const FILTERS = ['All', 'Running', 'Attention', 'Offline'];
const OFFLINE_AFTER_MS = 90_000;

function statusSlug(status = '') {
  return status.toLowerCase().replace(/[^a-z0-9]+/g, '-');
}

function statusClass(status = '') {
  return `status status-${statusSlug(status)}`;
}

function isOffline(machine, now = Date.now()) {
  const timestamp = new Date(machine.recordedAt).getTime();
  return !Number.isFinite(timestamp) || now - timestamp > OFFLINE_AFTER_MS;
}

function mergeMachine(current, incoming) {
  const index = current.findIndex((machine) => machine.machineId === incoming.machineId);
  if (index === -1) return [...current, incoming].sort((a, b) => a.machineId.localeCompare(b.machineId));

  const next = [...current];
  next[index] = incoming;
  return next;
}

function Dashboard() {
  const [machines, setMachines] = useState([]);
  const [error, setError] = useState('');
  const [activeView, setActiveView] = useState('Overview');
  const [lastUpdated, setLastUpdated] = useState(null);
  const [loading, setLoading] = useState(true);
  const [streamState, setStreamState] = useState('connecting');
  const [filter, setFilter] = useState('All');
  const [search, setSearch] = useState('');
  const [clock, setClock] = useState(Date.now());

  async function refresh() {
    try {
      const response = await fetch('/api/v1/machines/latest');
      if (!response.ok) throw new Error(`API error ${response.status}`);
      setMachines(await response.json());
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

    events.addEventListener('connected', () => {
      setStreamState('live');
      setError('');
    });

    events.addEventListener('heartbeat', () => {
      setStreamState('live');
    });

    events.addEventListener('telemetry', (event) => {
      try {
        const telemetry = JSON.parse(event.data);
        setMachines((current) => mergeMachine(current, telemetry));
        setLastUpdated(new Date());
        setStreamState('live');
        setError('');
      } catch {
        setError('Received invalid live telemetry');
      }
    });

    events.onerror = () => {
      setStreamState('reconnecting');
    };

    return () => {
      clearInterval(fallbackTimer);
      clearInterval(clockTimer);
      events.close();
    };
  }, []);

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
      const matchesFilter = filter === 'All'
        || (filter === 'Running' && !offline && status === 'RUNNING')
        || (filter === 'Attention' && !offline && status !== 'RUNNING')
        || (filter === 'Offline' && offline);
      const matchesSearch = !query
        || machine.machineId.toLowerCase().includes(query)
        || String(machine.lossCode || '').toLowerCase().includes(query);
      return matchesFilter && matchesSearch;
    });
  }, [machines, filter, search, clock]);

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark">A</div>
          <div><strong>AFMS</strong><span>Factory Intelligence</span></div>
        </div>

        <nav>
          {NAV_ITEMS.map((item) => (
            <button
              className={activeView === item ? 'nav-item active' : 'nav-item'}
              key={item}
              onClick={() => setActiveView(item)}
            >
              <span className="nav-dot" />{item}
            </button>
          ))}
        </nav>

        <div className={`sidebar-footer stream-${streamState}`}>
          <span className="system-dot" /> {streamState === 'live' ? 'Live connection' : 'Reconnecting'}
        </div>
      </aside>

      <main className="content">
        <header className="topbar">
          <div>
            <p className="eyebrow">AFMS v4.0 · Commit 0025</p>
            <h1>{activeView}</h1>
            <p className="subtitle">Real-time machine monitoring and production visibility</p>
          </div>
          <div className="topbar-actions">
            <div className="updated">Last telemetry<br /><strong>{lastUpdated ? lastUpdated.toLocaleTimeString() : 'Waiting...'}</strong></div>
            <button className="refresh-button" onClick={refresh} disabled={loading}>{loading ? 'Loading...' : 'Sync now'}</button>
          </div>
        </header>

        {error && <div className="alert"><strong>Connection warning:</strong> {error}</div>}

        <section className="summary-grid summary-grid-five">
          <article className="summary-card"><span>Total machines</span><strong>{summary.total}</strong><small>{summary.online} currently online</small></article>
          <article className="summary-card accent-running"><span>Running</span><strong>{summary.running}</strong><small>Producing now</small></article>
          <article className="summary-card accent-attention"><span>Needs attention</span><strong>{summary.attention}</strong><small>Idle, setup or stopped</small></article>
          <article className="summary-card accent-offline"><span>Offline</span><strong>{summary.offline}</strong><small>No update for 90 seconds</small></article>
          <article className="summary-card"><span>Total production</span><strong>{summary.production}</strong><small>{summary.rejects} rejects recorded</small></article>
        </section>

        <section className="section-heading machine-toolbar-heading">
          <div><p className="eyebrow">Live floor status</p><h2>Machine dashboard</h2></div>
          <span className={`live-badge ${streamState !== 'live' ? 'reconnecting' : ''}`}><i /> {streamState === 'live' ? 'Streaming live' : 'Reconnecting'}</span>
        </section>

        <section className="machine-toolbar">
          <div className="filter-group">
            {FILTERS.map((item) => (
              <button key={item} className={filter === item ? 'filter active' : 'filter'} onClick={() => setFilter(item)}>
                {item}{item === 'Offline' && summary.offline > 0 ? ` (${summary.offline})` : ''}
              </button>
            ))}
          </div>
          <input aria-label="Search machines" placeholder="Search machine or loss code" value={search} onChange={(event) => setSearch(event.target.value)} />
        </section>

        <section className="machine-grid">
          {!loading && machines.length === 0 && (
            <article className="empty-state"><div className="empty-icon">⌁</div><h2>No telemetry yet</h2><p>Waiting for an ESP8266 machine controller to send its first update.</p></article>
          )}

          {!loading && machines.length > 0 && visibleMachines.length === 0 && (
            <article className="empty-state"><div className="empty-icon">⌕</div><h2>No matching machines</h2><p>Change the filter or clear the search field.</p></article>
          )}

          {visibleMachines.map((machine) => {
            const offline = isOffline(machine, clock);
            const displayedStatus = offline ? 'OFFLINE' : machine.status;
            const goodCount = Math.max(0, Number(machine.productionCount || 0) - Number(machine.rejectCount || 0));
            const quality = Number(machine.productionCount) > 0
              ? ((goodCount / Number(machine.productionCount)) * 100).toFixed(1)
              : '100.0';
            return (
              <article className={`machine-card machine-${statusSlug(displayedStatus)}`} key={machine.machineId}>
                <div className="machine-header">
                  <div><p className="machine-label">Machine</p><h3>{machine.machineId}</h3></div>
                  <span className={statusClass(displayedStatus)}><i />{displayedStatus}</span>
                </div>

                <div className="metrics-grid">
                  <div><span>Production</span><strong>{machine.productionCount}</strong></div>
                  <div><span>Good parts</span><strong>{goodCount}</strong></div>
                  <div><span>Rejects</span><strong>{machine.rejectCount}</strong></div>
                  <div><span>Quality</span><strong>{quality}%</strong></div>
                  <div><span>Cycle time</span><strong>{(Number(machine.cycleTimeMs || 0) / 1000).toFixed(1)} s</strong></div>
                  <div><span>Signal age</span><strong>{Math.max(0, Math.round((clock - new Date(machine.recordedAt).getTime()) / 1000))} s</strong></div>
                </div>

                <div className="machine-footer">
                  <div><span>Current loss</span><strong>{machine.lossCode || 'No active loss'}</strong></div>
                  <time>Updated {new Date(machine.recordedAt).toLocaleString()}</time>
                </div>
              </article>
            );
          })}
        </section>
      </main>
    </div>
  );
}

createRoot(document.getElementById('root')).render(<Dashboard />);
