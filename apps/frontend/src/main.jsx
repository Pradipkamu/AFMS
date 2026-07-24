import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

const NAV_ITEMS = ['Overview', 'Machines', 'Production', 'Downtime', 'OEE', 'Reports'];

function statusClass(status = '') {
  return `status status-${status.toLowerCase().replace(/[^a-z0-9]+/g, '-')}`;
}

function Dashboard() {
  const [machines, setMachines] = useState([]);
  const [error, setError] = useState('');
  const [activeView, setActiveView] = useState('Overview');
  const [lastUpdated, setLastUpdated] = useState(null);
  const [loading, setLoading] = useState(true);

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
    const timer = setInterval(refresh, 5000);
    return () => clearInterval(timer);
  }, []);

  const summary = useMemo(() => {
    const running = machines.filter((machine) => machine.status?.toUpperCase() === 'RUNNING').length;
    return {
      total: machines.length,
      running,
      stopped: machines.length - running,
      production: machines.reduce((sum, machine) => sum + Number(machine.productionCount || 0), 0),
      rejects: machines.reduce((sum, machine) => sum + Number(machine.rejectCount || 0), 0),
    };
  }, [machines]);

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

        <div className="sidebar-footer">
          <span className="system-dot" /> System online
        </div>
      </aside>

      <main className="content">
        <header className="topbar">
          <div>
            <p className="eyebrow">AFMS v4.0</p>
            <h1>{activeView}</h1>
            <p className="subtitle">Automated Factory Monitoring System</p>
          </div>
          <div className="topbar-actions">
            <div className="updated">Last update<br /><strong>{lastUpdated ? lastUpdated.toLocaleTimeString() : 'Waiting...'}</strong></div>
            <button className="refresh-button" onClick={refresh} disabled={loading}>{loading ? 'Loading...' : 'Refresh data'}</button>
          </div>
        </header>

        {error && <div className="alert"><strong>Connection warning:</strong> {error}</div>}

        <section className="summary-grid">
          <article className="summary-card"><span>Total machines</span><strong>{summary.total}</strong><small>Connected assets</small></article>
          <article className="summary-card"><span>Running</span><strong>{summary.running}</strong><small>Active now</small></article>
          <article className="summary-card"><span>Stopped / idle</span><strong>{summary.stopped}</strong><small>Needs attention</small></article>
          <article className="summary-card"><span>Total production</span><strong>{summary.production}</strong><small>{summary.rejects} rejects recorded</small></article>
        </section>

        <section className="section-heading">
          <div><p className="eyebrow">Live floor status</p><h2>Machine overview</h2></div>
          <span className="live-badge"><i /> Live</span>
        </section>

        <section className="machine-grid">
          {!loading && machines.length === 0 && (
            <article className="empty-state"><div className="empty-icon">⌁</div><h2>No telemetry yet</h2><p>Waiting for an ESP8266 machine controller to send its first update.</p></article>
          )}

          {machines.map((machine) => {
            const goodCount = Math.max(0, Number(machine.productionCount || 0) - Number(machine.rejectCount || 0));
            return (
              <article className="machine-card" key={machine.machineId}>
                <div className="machine-header">
                  <div><p className="machine-label">Machine</p><h3>{machine.machineId}</h3></div>
                  <span className={statusClass(machine.status)}><i />{machine.status}</span>
                </div>

                <div className="metrics-grid">
                  <div><span>Production</span><strong>{machine.productionCount}</strong></div>
                  <div><span>Good parts</span><strong>{goodCount}</strong></div>
                  <div><span>Rejects</span><strong>{machine.rejectCount}</strong></div>
                  <div><span>Cycle time</span><strong>{(Number(machine.cycleTimeMs || 0) / 1000).toFixed(1)} s</strong></div>
                </div>

                <div className="machine-footer">
                  <div><span>Current loss</span><strong>{machine.lossCode || 'No active loss'}</strong></div>
                  <time>{new Date(machine.recordedAt).toLocaleString()}</time>
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
