import { useEffect, useMemo, useState } from "react";

type Tab = "overview" | "clean" | "temp" | "procs" | "stream" | "settings";

const TABS: { id: Tab; label: string }[] = [
  { id: "overview", label: "Обзор" },
  { id: "clean", label: "Очистка" },
  { id: "temp", label: "Временные файлы" },
  { id: "procs", label: "Процессы" },
  { id: "stream", label: "Стрим" },
  { id: "settings", label: "Настройки" },
];

const ACTIONS = [
  "Обрезка рабочих множеств",
  "Standby List (низк.)",
  "Кэш файлов ОС",
  "Standby List (все)",
  "Сброс изменённых страниц",
  "Аллокация памяти",
  "Сброс Суперфетча",
  "Combine Pages",
  "Сброс реестра",
  "Чистка Pagefile",
];

const CATS: { group: string; name: string; mb: number; files: number; clean: boolean }[] = [
  { group: "Пользователь", name: "Пользовательские Temp", mb: 842, files: 1204, clean: true },
  { group: "Пользователь", name: "Chrome Cache", mb: 1260, files: 8400, clean: true },
  { group: "Пользователь", name: "Edge Cache", mb: 410, files: 2201, clean: true },
  { group: "Пользователь", name: "Discord Cache", mb: 186, files: 640, clean: true },
  { group: "Пользователь", name: "NVIDIA DXCache", mb: 920, files: 310, clean: true },
  { group: "Система", name: "Системные Temp", mb: 640, files: 880, clean: true },
  { group: "Система", name: "Prefetch", mb: 48, files: 210, clean: true },
  { group: "Система", name: "Windows Update Cache", mb: 1800, files: 96, clean: true },
  { group: "Система", name: "Crash Dumps", mb: 256, files: 4, clean: true },
  { group: "Система", name: "Event Log Archives", mb: 120, files: 40, clean: false },
  { group: "Система", name: "Installer Patch Cache", mb: 340, files: 18, clean: false },
];

type Proc = { name: string; pid: number; mb: number; locked: boolean };

const PROCS: Proc[] = [
  { name: "chrome.exe", pid: 4412, mb: 1840, locked: false },
  { name: "Discord.exe", pid: 902, mb: 640, locked: false },
  { name: "explorer.exe", pid: 4416, mb: 210, locked: true },
  { name: "cs2.exe", pid: 12044, mb: 3100, locked: false },
  { name: "dwm.exe", pid: 1288, mb: 180, locked: true },
  { name: "MsMpEng.exe", pid: 2210, mb: 260, locked: true },
  { name: "Code.exe", pid: 7740, mb: 890, locked: false },
  { name: "steamwebhelper.exe", pid: 6501, mb: 420, locked: false },
];

function gb(n: number) {
  return `${n.toFixed(2)} ГБ`;
}

function fmtMb(mb: number) {
  if (mb >= 1024) return `${(mb / 1024).toFixed(2)} ГБ`;
  if (mb >= 1) return `${mb.toFixed(0)} МБ`;
  return "—";
}

function IconBolt() {
  return (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor" aria-hidden>
      <path d="M13 2 L4 14 h7 l-1 8 10-14 h-7 z" />
    </svg>
  );
}

function Donut({ pct, size = 168, stroke = 14, color = "#34d399" }: { pct: number; size?: number; stroke?: number; color?: string }) {
  const r = (size - stroke) / 2;
  const c = 2 * Math.PI * r;
  const p = Math.max(0, Math.min(100, pct)) / 100;
  return (
    <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} aria-hidden>
      <circle cx={size / 2} cy={size / 2} r={r} fill="none" stroke="#2e323c" strokeWidth={stroke} />
      <circle
        cx={size / 2}
        cy={size / 2}
        r={r}
        fill="none"
        stroke={color}
        strokeWidth={stroke}
        strokeLinecap="round"
        strokeDasharray={`${c * p} ${c}`}
        transform={`rotate(-90 ${size / 2} ${size / 2})`}
      />
    </svg>
  );
}

export function RamApp() {
  const [tab, setTab] = useState<Tab>("overview");
  const [total] = useState(15.71);
  const [avail, setAvail] = useState(12.08);
  const [standby, setStandby] = useState(0.62);
  const [cache, setCache] = useState(0.24);
  const [cpu, setCpu] = useState(6);
  const [hist, setHist] = useState<number[]>(() => Array.from({ length: 28 }, () => 23));
  const [autoClean, setAutoClean] = useState(true);
  const [stream, setStream] = useState(false);
  const [session, setSession] = useState(5.78);
  const [lastFreed, setLastFreed] = useState(0.13);
  const [lastWhen, setLastWhen] = useState("Только что");
  const [cleaning, setCleaning] = useState(false);
  const [mode, setMode] = useState(2);
  const [acts, setActs] = useState<boolean[]>(() => ACTIONS.map((_, i) => [0, 1, 2, 3, 8].includes(i)));
  const [steps, setSteps] = useState<{ name: string; delta: number }[]>([]);
  const [scanned, setScanned] = useState(false);
  const [scanning, setScanning] = useState(false);
  const [checked, setChecked] = useState<boolean[]>(() => CATS.map((c) => c.clean));
  const [freedFiles, setFreedFiles] = useState(0);
  const [q, setQ] = useState("");
  const [sort, setSort] = useState<"ram" | "pid" | "name">("ram");
  const [sel, setSel] = useState(0);
  const [closeApps, setCloseApps] = useState<string[]>(["Telegram", "Discord", "Slack"]);
  const [boostApps, setBoostApps] = useState<string[]>(["cs2", "dota2"]);
  const [draftClose, setDraftClose] = useState("");
  const [draftBoost, setDraftBoost] = useState("");
  const [threshold, setThreshold] = useState(80);
  const [notifs, setNotifs] = useState(true);
  const [toast, setToast] = useState("");

  useEffect(() => {
    const id = setInterval(() => {
      setCpu((v) => Math.max(2, Math.min(92, v + (Math.random() - 0.48) * 6)));
      setAvail((v) => Math.max(1.2, Math.min(total - 0.4, v + (Math.random() - 0.5) * 0.04)));
      setHist((h) => [...h.slice(1), ((total - avail) / total) * 100]);
    }, 1200);
    return () => clearInterval(id);
  }, [avail, total]);

  const used = Math.max(0, total - avail);
  const usage = (used / total) * 100;
  const pressure = usage < 55 && avail >= 4 ? "Свободно" : usage < 85 ? "Норма" : "Высокое";
  const pressColor = pressure === "Свободно" ? "#34d399" : pressure === "Норма" ? "#fbbf24" : "#f87171";
  const donutColor = usage > 85 ? "#f87171" : usage > 60 ? "#fbbf24" : "#34d399";

  const points = useMemo(() => {
    const w = 460;
    const h = 84;
    return hist
      .map((v, i) => {
        const x = (i / Math.max(1, hist.length - 1)) * w;
        const y = h - (Math.max(0, Math.min(100, v)) / 100) * (h - 6) - 2;
        return `${x.toFixed(1)},${y.toFixed(1)}`;
      })
      .join(" ");
  }, [hist]);

  function ping(msg: string) {
    setToast(msg);
    setTimeout(() => setToast(""), 2400);
  }

  function runClean() {
    if (cleaning) return;
    setCleaning(true);
    setTab("clean");
    const enabled = ACTIONS.map((name, i) => ({ name, on: acts[i] })).filter((a) => a.on);
    const plan = enabled.length ? enabled : [{ name: "Рабочие множества", on: true }];
    window.setTimeout(() => {
      const made = plan.map((a) => ({
        name: a.name,
        delta: Math.round((0.02 + Math.random() * 0.08) * 100) / 100,
      }));
      const sum = made.reduce((s, x) => s + x.delta, 0);
      setSteps(made);
      setAvail((v) => Math.min(total - 0.3, v + sum));
      setStandby((v) => Math.max(0.05, v * 0.45));
      setCache((v) => Math.max(0.04, v * 0.5));
      setSession((v) => v + sum);
      setLastFreed(sum);
      setLastWhen("Только что");
      setCleaning(false);
      ping(`Освобождено ${sum.toFixed(2)} ГБ`);
    }, 900);
  }

  function scan() {
    setScanning(true);
    setScanned(false);
    window.setTimeout(() => {
      setScanning(false);
      setScanned(true);
      setChecked(CATS.map((c) => c.clean));
    }, 700);
  }

  const selMb = CATS.reduce((s, c, i) => s + (checked[i] && c.clean ? c.mb : 0), 0);
  const selFiles = CATS.reduce((s, c, i) => s + (checked[i] && c.clean ? c.files : 0), 0);

  const view = PROCS.filter((p) => p.name.toLowerCase().includes(q.toLowerCase())).sort((a, b) => {
    if (sort === "pid") return a.pid - b.pid;
    if (sort === "name") return a.name.localeCompare(b.name);
    return b.mb - a.mb;
  });

  let lastGroup = "";

  return (
    <main className="rb">
      <section className="rb-win">
        <header className="rb-cap">
          <span className="rb-mark" aria-hidden>
            <svg width="12" height="12" viewBox="0 0 12 12">
              <circle cx="6" cy="6.4" r="3.4" fill="none" stroke="#f3f4f6" strokeWidth="1.4" strokeDasharray="16 6" strokeLinecap="round" />
              <path d="M6 2.1 V5.2" stroke="#f3f4f6" strokeWidth="1.4" strokeLinecap="round" />
            </svg>
          </span>
          <b>RamBoost Pro</b>
          <span className="rb-cap-sp" />
          <span className="rb-winbtns" aria-hidden>
            <i>─</i>
            <i>□</i>
            <i>×</i>
          </span>
        </header>
        <div className="rb-body">
          <nav className="rb-tabs" aria-label="Разделы">
            {TABS.map((t) => (
              <button key={t.id} data-on={tab === t.id ? "1" : "0"} onClick={() => setTab(t.id)}>
                {t.label}
              </button>
            ))}
          </nav>

          {tab === "overview" && (
            <>
              <div className="rb-stats">
                <Stat color="#34d399" label="Доступно" value={gb(avail)} icon="ram" />
                <Stat color="#f3f4f6" label="Всего" value={gb(total)} icon="shield" />
                <Stat color="#fbbf24" label="Standby" value={gb(standby)} icon="doc" />
                <Stat color="#a78bfa" label="В кэше" value={gb(cache)} icon="bolt" />
              </div>
              <div className="rb-grid">
                <article className="rb-card" style={{ ["--acc" as string]: "#60a5fa" }}>
                  <h2>Использование RAM</h2>
                  <div className="rb-donut-wrap">
                    <Donut pct={usage} color={donutColor} />
                    <div className="rb-donut-label">
                      <b>{Math.round(usage)}%</b>
                      <small>Занято</small>
                    </div>
                  </div>
                  <div className="rb-legs">
                    <span>Исп: {used.toFixed(1)} ГБ</span>
                    <span>Св: {avail.toFixed(1)} ГБ</span>
                  </div>
                </article>
                <article className="rb-card" style={{ ["--acc" as string]: "#a78bfa" }}>
                  <h2>Состояние системы</h2>
                  <div className="rb-cpu-row">
                    <div className="rb-cpu">
                      <Donut pct={cpu} size={92} stroke={8} color="#22d3ee" />
                      <div className="rb-donut-label">
                        <b>{Math.round(cpu)}%</b>
                        <small>CPU</small>
                      </div>
                    </div>
                    <div className="rb-press">
                      <small>Давление памяти</small>
                      <b style={{ color: pressColor }}>{pressure}</b>
                    </div>
                  </div>
                  <div className="rb-bar" aria-hidden>
                    <i style={{ width: `${usage}%` }} />
                  </div>
                  <svg className="rb-graph" viewBox="0 0 460 84" preserveAspectRatio="none" aria-hidden>
                    <polyline fill="none" stroke={donutColor} strokeWidth="2" points={points} />
                  </svg>
                </article>
              </div>
              <div className="rb-strip">
                <div className="rb-dots">
                  <span>
                    <i style={{ background: autoClean ? "#34d399" : "#6b7280" }} />
                    Авто-очистка: {autoClean ? "ВКЛ" : "ВЫКЛ"}
                  </span>
                  <span>
                    <i style={{ background: stream ? "#34d399" : "#6b7280" }} />
                    Стрим: {stream ? "ВКЛ" : "ВЫКЛ"}
                  </span>
                </div>
                <div className="rb-right">
                  <span>
                    Последняя: {lastWhen} · освобождено {lastFreed.toFixed(2)} ГБ
                  </span>
                  <b>За сессию: {session.toFixed(2)} ГБ</b>
                </div>
              </div>
              <div className="rb-actions">
                <button className="rb-primary" onClick={runClean} disabled={cleaning}>
                  {cleaning ? "Очистка..." : "Очистить сейчас"}
                </button>
                <button
                  className={stream ? "rb-on" : "rb-primary"}
                  onClick={() => {
                    setStream((v) => !v);
                    if (!stream) runClean();
                  }}
                >
                  Стрим: {stream ? "ВКЛ" : "ВЫКЛ"}
                </button>
              </div>
            </>
          )}

          {tab === "clean" && (
            <div className="rb-panel">
              <div className="rb-mode">
                <strong>Режим очистки</strong>
                <div className="rb-pills">
                  {["Мягкая", "Умная", "Агрессивная"].map((name, i) => (
                    <button key={name} data-on={mode === i ? "1" : "0"} onClick={() => setMode(i)}>
                      {name}
                    </button>
                  ))}
                </div>
              </div>
              <div className="rb-checks">
                {ACTIONS.map((name, i) => (
                  <label key={name}>
                    <input
                      type="checkbox"
                      checked={acts[i]}
                      onChange={() => setActs((a) => a.map((v, idx) => (idx === i ? !v : v)))}
                    />
                    {name}
                  </label>
                ))}
              </div>
              <button className="rb-primary" onClick={runClean} disabled={cleaning}>
                {cleaning ? "Очистка выполняется..." : "Запустить очистку"}
              </button>
              <div className="rb-log">
                {steps.length === 0 && <span>Отмеченные действия и только они попадут в отчёт.</span>}
                {steps.map((s) => (
                  <div key={s.name}>
                    <span>{s.name}</span>
                    <span className="rb-pos">+{s.delta.toFixed(2)} ГБ</span>
                  </div>
                ))}
              </div>
            </div>
          )}

          {tab === "temp" && (
            <div className="rb-panel">
              <div className="rb-rowbtns">
                <button className="rb-primary" onClick={scan} disabled={scanning}>
                  {scanning ? "Сканирование..." : "Сканировать"}
                </button>
                <button
                  className="rb-primary"
                  disabled={!scanned}
                  onClick={() => {
                    setFreedFiles(selFiles);
                    setChecked(CATS.map(() => false));
                    ping(`Удалено файлов: ${selFiles}`);
                  }}
                >
                  Удалить выбранное
                </button>
                <button className="rb-ghost" onClick={() => setChecked(CATS.map((c) => c.clean))}>
                  Выбрать все
                </button>
                <button className="rb-ghost" onClick={() => setChecked(CATS.map(() => false))}>
                  Снять выбор
                </button>
                <span className="rb-note">
                  {scanned
                    ? `Можно очистить: ${fmtMb(selMb)} · ${selFiles} файлов`
                    : "Категории «только просмотр» в сумму не входят"}
                </span>
              </div>
              <div className="rb-list">
                {scanning && <div className="rb-note">Идёт сканирование кэшей и журналов...</div>}
                {scanned &&
                  CATS.map((c, i) => {
                    const head = c.group !== lastGroup;
                    lastGroup = c.group;
                    return (
                      <div key={c.name}>
                        {head && <div className="rb-grp">{c.group.toUpperCase()}</div>}
                        <label className="rb-item">
                          <input
                            type="checkbox"
                            disabled={!c.clean}
                            checked={checked[i]}
                            onChange={() => setChecked((arr) => arr.map((v, idx) => (idx === i ? !v : v)))}
                          />
                          <span>
                            {c.name}
                            <small>{c.clean ? `${c.files} файл.` : "только просмотр · не удаляется"}</small>
                          </span>
                          <b>{fmtMb(c.mb)}</b>
                        </label>
                      </div>
                    );
                  })}
                {freedFiles > 0 && <div className="rb-pos">Освобождено файлов: {freedFiles}</div>}
              </div>
            </div>
          )}

          {tab === "procs" && (
            <div className="rb-panel">
              <div className="rb-rowbtns">
                <button className="rb-ghost" onClick={() => ping("Список обновлён")}>
                  Обновить
                </button>
                <button className="rb-ghost" onClick={() => ping(`${view[sel]?.name ?? "Процесс"}: приоритет High`)}>
                  Приоритет High
                </button>
                <button
                  className="rb-primary"
                  onClick={() => ping(view[sel]?.locked ? "Процесс защищён" : `Завершить ${view[sel]?.name}?`)}
                >
                  Завершить
                </button>
                <input className="rb-search" placeholder="Поиск процесса" value={q} onChange={(e) => setQ(e.target.value)} />
              </div>
              <div className="rb-hdr">
                <button onClick={() => setSort("name")}>Имя {sort === "name" ? "▲" : ""}</button>
                <button onClick={() => setSort("pid")}>PID {sort === "pid" ? "▲" : ""}</button>
                <button onClick={() => setSort("ram")}>RAM {sort === "ram" ? "▼" : ""}</button>
              </div>
              <div className="rb-list">
                {view.map((p, i) => (
                  <button key={p.pid} className="rb-proc" data-on={sel === i ? "1" : "0"} onClick={() => setSel(i)}>
                    <span style={{ color: p.locked ? "#fbbf24" : undefined }}>
                      {p.locked ? "● " : ""}
                      {p.name}
                    </span>
                    <small>PID {p.pid}</small>
                    <b>{p.mb} МБ</b>
                  </button>
                ))}
              </div>
            </div>
          )}

          {tab === "stream" && (
            <div className="rb-two">
              <div className="rb-panel">
                <h2 style={{ marginTop: 0 }}>Закрыть фоновые приложения</h2>
                <button className={stream ? "rb-on" : "rb-primary"} onClick={() => setStream((v) => !v)}>
                  Стрим-режим: {stream ? "ВКЛ" : "ВЫКЛ"}
                </button>
                {closeApps.map((n) => (
                  <div className="rb-chip" key={n}>
                    <span>{n}</span>
                    <button aria-label={`Убрать ${n}`} onClick={() => setCloseApps((a) => a.filter((x) => x !== n))}>
                      ×
                    </button>
                  </div>
                ))}
                <div className="rb-field">
                  <input className="rb-search" value={draftClose} onChange={(e) => setDraftClose(e.target.value)} placeholder="Имя процесса" />
                  <button
                    className="rb-ghost"
                    onClick={() => {
                      const v = draftClose.trim();
                      if (v && !closeApps.includes(v)) setCloseApps((a) => [...a, v]);
                      setDraftClose("");
                    }}
                  >
                    Добавить
                  </button>
                </div>
              </div>
              <div className="rb-panel">
                <h2 style={{ marginTop: 0 }}>Повысить приоритет</h2>
                {boostApps.map((n) => (
                  <div className="rb-chip" key={n}>
                    <span>{n}</span>
                    <button aria-label={`Убрать ${n}`} onClick={() => setBoostApps((a) => a.filter((x) => x !== n))}>
                      ×
                    </button>
                  </div>
                ))}
                <div className="rb-field">
                  <input className="rb-search" value={draftBoost} onChange={(e) => setDraftBoost(e.target.value)} placeholder="Игра или приложение" />
                  <button
                    className="rb-ghost"
                    onClick={() => {
                      const v = draftBoost.trim();
                      if (v && !boostApps.includes(v)) setBoostApps((a) => [...a, v]);
                      setDraftBoost("");
                    }}
                  >
                    Добавить
                  </button>
                </div>
                <button className="rb-primary" style={{ marginTop: 12 }} onClick={() => ping(`Буст применён: ${boostApps.length}`)}>
                  Применить буст
                </button>
              </div>
            </div>
          )}

          {tab === "settings" && (
            <div className="rb-panel">
              <div className="rb-checks">
                <label className="rb-check">
                  <input type="checkbox" checked={autoClean} onChange={() => setAutoClean((v) => !v)} />
                  Авто-очистка по порогу
                </label>
                <label className="rb-check">
                  <input type="checkbox" checked={notifs} onChange={() => setNotifs((v) => !v)} />
                  Уведомления об очистке
                </label>
              </div>
              <div className="rb-slider">
                <span>Порог {threshold}%</span>
                <input
                  type="range"
                  min={40}
                  max={95}
                  value={threshold}
                  onChange={(e) => setThreshold(Number(e.target.value))}
                />
              </div>
              <button className="rb-primary" onClick={() => ping(notifs ? "Настройки сохранены" : "Настройки сохранены, уведомления выключены")}>
                Сохранить настройки
              </button>
              <p className="rb-demo">
                Это наглядная копия интерфейса. Очистка памяти и удаление файлов выполняются только в программе для Windows — архив с исправленными исходниками приложен к ответу.
              </p>
            </div>
          )}

          {toast && <p className="rb-demo">{toast}</p>}
        </div>
      </section>
    </main>
  );
}

function Stat({ color, label, value, icon }: { color: string; label: string; value: string; icon: string }) {
  return (
    <article className="rb-stat" style={{ ["--acc" as string]: color }}>
      <span className="rb-ico" aria-hidden>
        {icon === "bolt" ? <IconBolt /> : icon === "shield" ? "▣" : icon === "doc" ? "▤" : "▦"}
      </span>
      <div>
        <strong>{value}</strong>
        <span>{label}</span>
      </div>
    </article>
  );
}
