import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

function Dashboard() {
  const [machines, setMachines] = useState([]);
  const [error, setError] = useState('');

  async function refresh() {
    try {
      const response = await fetch('/api/v1/machines/latest');
      if (!response.ok) throw new Error(`API error ${response.status}`);
      setMachines(await response.json());
      setError('');
    } catch (err) {
      setError(err.message);
    }
  }

  useEffect(() => {
    refresh();
    const timer = setInterval(refresh, 5000);
    return () => clearInterval(timer);
  }, []);

  return (
    <main>
      <header>
        <div><h1>AFMS</h1><p>Automated Factory Monitoring System</p></div>
        <button onClick={refresh}>Refresh</button>
      </header>
      {error && <p className="error">{error}</p>}
      <section className="grid">
        {machines.length === 0 && <article><h2>No telemetry yet</h2><p>Waiting for an ESP8266 machine controller.</p></article>}
        {machines.map((machine) => (
          <article key={machine.machineId}>
            <div className="title"><h2>{machine.machineId}</h2><span>{machine.status}</span></div>
            <dl>
              <div><dt>Production</dt><dd>{machine.productionCount}</dd></div>
              <div><dt>Rejects</dt><dd>{machine.rejectCount}</dd></div>
              <div><dt>Cycle</dt><dd>{machine.cycleTimeMs} ms</dd></div>
            </dl>
            <p>Loss: {machine.lossCode || 'None'}</p>
            <small>{new Date(machine.recordedAt).toLocaleString()}</small>
          </article>
        ))}
      </section>
    </main>
  );
}

createRoot(document.getElementById('root')).render(<Dashboard />);
