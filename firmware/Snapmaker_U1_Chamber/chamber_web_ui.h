#pragma once

#include <pgmspace.h>

// WebGUI markup, styles and browser code served by handleRoot().
static const char CHAMBER_PAGE_TOP[] PROGMEM = R"chamberUI(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="color-scheme" content="dark">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'">
  <title>Snapmaker U1 Chamber Control</title>
)chamberUI";

static const char CHAMBER_PAGE_BODY[] PROGMEM = R"chamberUI(  <style>
    :root {
      color-scheme: dark;
      --bg: #0c121c;
      --top: #101924;
      --panel: #151f2d;
      --field: #101924;
      --line: #2a394b;
      --text: #eef4fc;
      --muted: #a6b9cf;
      --dim: #8399b2;
      --accent: #70d7ca;
      --green: #85dfb3;
      --warm: #f4c382;
      --danger: #ff939f;
      --purple: #bba9ed;
      --bed: #ec9d86;
      --radius: 13px;
      font: 16px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif;
    }
    *, *::before, *::after { box-sizing: border-box; }
    body { margin: 0; background: var(--bg); color: var(--text); }
    [hidden] { display: none !important; }
    button, input, select { font: inherit; }
    button, a, input, select, summary, canvas { -webkit-tap-highlight-color: transparent; }
    :focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
    h1, h2, h3, p, dl, dd { margin: 0; }
    a { color: inherit; }
    p, dd, output { overflow-wrap: anywhere; }
    h1 { font-size: clamp(1.8rem, 3vw, 2.35rem); font-weight: 660; line-height: 1.15; letter-spacing: -.042em; }
    h2 { font-size: 1.04rem; font-weight: 660; letter-spacing: -.012em; }
    h3 { font-size: .94rem; font-weight: 635; }
    .shell { width: min(100%, 1392px); margin-inline: auto; padding-inline: clamp(1rem, 3vw, 2.4rem); }
    .header { background: var(--top); border-bottom: 1px solid var(--line); }
    .topbar { min-height: 73px; display: flex; align-items: center; gap: 1.5rem; }
    .brand { display: inline-flex; align-items: center; gap: .73rem; text-decoration: none; margin-right: auto; }
    .brand-mark { width: 40px; height: 40px; display: grid; place-items: center; flex: none; border-radius: 9px; border: 1px solid #3d696e; background: #1a353d; color: #9ae8dc; font-weight: 750; letter-spacing: -.08em; }
    .brand-text { display: grid; gap: .08rem; line-height: 1.25; white-space: nowrap; }
    .brand-text small { color: var(--accent); font-size: .68rem; font-weight: 700; letter-spacing: .12em; }
    .brand-text strong { font-size: .99rem; font-weight: 650; }
    .nav { display: flex; gap: .35rem; align-items: center; }
    .nav a { padding: .49rem .85rem; border: 1px solid transparent; border-radius: 8px; text-decoration: none; color: var(--muted); font-size: .875rem; font-weight: 610; }
    .nav a:hover { background: #1a2939; color: var(--text); }
    .nav a[aria-current="page"] { background: #183039; border-color: #365762; color: #a4e9df; }
    .demo-button { color: var(--warm); background: #282521; border-color: #554a38; white-space: nowrap; }
    .demo-button:hover { background: #332d24; }
    main { padding-block: clamp(1.5rem, 2.6vw, 2.25rem) 2.75rem; }
    .page-heading { display: flex; align-items: flex-end; justify-content: space-between; gap: .75rem; margin-bottom: 1.25rem; }
    .live-label { display: inline-flex; gap: .48rem; align-items: center; color: var(--muted); font-size: .82rem; white-space: nowrap; }
    .dot, .legend-dot { width: .52rem; height: .52rem; border-radius: 50%; background: currentColor; display: inline-block; flex: none; }
    .dot { color: var(--accent); }
    .status-grid { display: grid; grid-template-columns: repeat(6, minmax(0, 1fr)); background: var(--top); border: 1px solid var(--line); border-radius: var(--radius); overflow: hidden; margin-bottom: 1rem; }
    .status-grid > div { padding: .78rem .95rem; min-width: 0; }
    .status-grid > div + div { border-left: 1px solid var(--line); }
    .status-grid dt, .metric-label { color: var(--muted); font-size: .82rem; font-weight: 560; }
    .status-grid dd { margin-top: .22rem; color: var(--text); font-size: .9rem; font-weight: 660; font-variant-numeric: tabular-nums; }
    [data-tone="ok"] { --tone: var(--green); }
    [data-tone="warm"] { --tone: var(--warm); }
    [data-tone="bad"] { --tone: var(--danger); }
    [data-tone="muted"] { --tone: var(--muted); }
    [data-tone="accent"] { --tone: var(--accent); }
    .status-grid dd[data-tone], .tone { color: var(--tone, var(--muted)); }
    .dashboard { display: grid; grid-template-columns: minmax(290px, .94fr) minmax(0, 2.13fr); gap: 1rem; align-items: start; }
    .stack, .settings-grid { display: grid; gap: 1rem; min-width: 0; }
    .card { min-width: 0; border: 1px solid var(--line); border-radius: var(--radius); background: var(--panel); padding: 1rem 1.08rem; }
    .card-head { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: .55rem .75rem; }
    .card-head + .subtext, .subtext { color: var(--muted); font-size: .86rem; }
    .card-head .subtext { margin-top: .25rem; }
    .pill { display: inline-flex; align-items: center; gap: .37rem; border: 1px solid var(--line); background: var(--field); border-radius: 99px; padding: .24rem .54rem; color: var(--tone, var(--muted)); font-size: .72rem; font-weight: 680; white-space: nowrap; }
    .pill .dot { color: inherit; width: .37rem; height: .37rem; }
    .thermal-card { padding-bottom: 1.08rem; border-top-color: #416b70; }
    .gauge { --sweep: 198deg; position: relative; width: min(100%, 220px); aspect-ratio: 1; margin: .9rem auto -.35rem; }
    .gauge-arc { position: absolute; inset: 3px; border-radius: 50%; background: conic-gradient(from 225deg, var(--accent) 0deg var(--sweep), #2b3c4f var(--sweep) 270deg, transparent 270deg); }
    .gauge-arc::after { content: ""; position: absolute; inset: 9px; border-radius: 50%; background: var(--panel); }
    .gauge-text { display: grid; justify-items: center; position: absolute; inset: 0; align-content: center; padding-bottom: .72rem; }
    .temperature { color: var(--text); font-size: clamp(3.3rem, 4.6vw, 4.55rem); line-height: 1; font-variant-numeric: tabular-nums; font-weight: 680; letter-spacing: -.068em; }
    .temperature small { color: var(--muted); font-size: .41em; font-weight: 550; vertical-align: .2em; margin-left: .17em; letter-spacing: -.04em; }
    .gauge-label { margin-top: .3rem; font-size: .78rem; letter-spacing: .045em; color: var(--muted); }
    .gauge-target { font-size: .91rem; color: var(--accent); font-weight: 660; margin-top: .67rem; }
    .gauge-scale { display: flex; position: absolute; justify-content: space-between; inset-inline: 10%; bottom: 9%; color: var(--dim); font-size: .76rem; font-variant-numeric: tabular-nums; }
    .progress-title { display: flex; justify-content: space-between; gap: .7rem; font-size: .83rem; color: var(--muted); }
    .progress-title strong { color: var(--accent); font-weight: 650; }
    .progress-track { height: 5px; border-radius: 99px; background: #2a394b; overflow: hidden; margin-top: .44rem; }
    .progress-fill { width: 87%; height: 100%; background: var(--accent); border-radius: inherit; }
    .io-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: .52rem; margin-top: .95rem; }
    .io-cell { display: flex; gap: .35rem; align-items: center; justify-content: space-between; padding: .49rem .62rem; border-radius: 7px; border: 1px solid var(--line); background: var(--field); color: var(--muted); font-size: .84rem; }
    .io-cell strong { color: var(--tone, var(--muted)); font-size: .75rem; font-weight: 750; }
    .button { min-height: 42px; display: inline-flex; align-items: center; gap: .47rem; justify-content: center; padding: .43rem .82rem; border: 1px solid var(--line); border-radius: 8px; background: #1a2838; color: var(--text); font-size: .87rem; font-weight: 650; line-height: 1.22; cursor: pointer; }
    .button:hover { background: #213449; border-color: #435a71; }
    .button:disabled { opacity: .48; cursor: default; }
    .button.primary { background: #82d8ca; border-color: #82d8ca; color: #102723; }
    .button.primary:hover { background: #9be6da; }
    .button.danger { background: #38232d; border-color: #78505d; color: #ffb1bb; }
    .button.danger:hover { background: #4b2835; }
    .button.demo-button { color: var(--warm); background: #282521; border-color: #554a38; }
    .button.demo-button:hover { background: #383023; }
    .button.power { width: 100%; min-height: 45px; margin-top: .92rem; font-size: .9rem; letter-spacing: .06em; }
    .button.power[data-running="true"] { color: #2a151b; background: #f08794; border-color: #f08794; }
    .button.power[data-running="true"]:hover { background: #ffa0ab; }
    .power-icon { display: inline-block; width: .63rem; height: .63rem; background: currentColor; border-radius: 1px; }
    .power[data-running="false"] .power-icon { clip-path: polygon(0 0,100% 50%,0 100%); }
    .state-description { margin-top: .56rem; text-align: center; font-size: .79rem; color: var(--muted); min-height: 1.15rem; }
    .segmented { margin-top: .78rem; display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); padding: .24rem; gap: .24rem; background: var(--field); border: 1px solid var(--line); border-radius: 8px; }
    .segmented .button { min-height: 37px; background: transparent; color: var(--muted); border-color: transparent; font-size: .79rem; }
    .segmented .button:hover { color: var(--text); background: #1b2b3a; }
    .segmented .button[aria-pressed="true"] { color: #aff0e6; background: #1c3841; border-color: #3a626a; }
    .mode-state { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: .3rem .6rem; margin-top: .55rem; min-height: 1.25rem; font-size: .77rem; color: var(--muted); }
    .stepper { display: grid; grid-template-columns: 44px minmax(0, 1fr) 44px; gap: .43rem; align-items: center; margin-block: .66rem; }
    .stepper .button { padding: 0; font-size: 1.35rem; }
    .stepper output { text-align: center; color: var(--accent); font-size: 1.52rem; letter-spacing: -.045em; font-weight: 660; font-variant-numeric: tabular-nums; }
    .presets { display: grid; grid-template-columns: repeat(3,minmax(0,1fr)); gap: .4rem; }
    .presets .button { display: grid; gap: .16rem; justify-items: center; padding: .44rem .15rem; min-height: 49px; }
    .presets small { color: var(--muted); font-size: .72rem; font-weight: 500; white-space: nowrap; }
    .metrics { display: grid; grid-template-columns: repeat(4,minmax(0,1fr)); gap: .62rem; }
    .metric { min-width: 0; min-height: 113px; padding: .76rem .78rem; border: 1px solid var(--line); border-radius: 10px; background: var(--panel); border-top-color: #486277; }
    .metric[data-series="target"] { border-top-color: #987a51; }
    .metric[data-series="delta"] { background: #172a35; border-top-color: #4e8c87; }
    .metric[data-series="cavity"] { border-top-color: #746598; }
    .metric[data-series="bed"] { border-top-color: #916354; }
    .metric dd { color: var(--tone, var(--text)); }
    .metric .metric-value { margin-top: .33rem; font-size: clamp(1.25rem, 2.4vw, 1.8rem); line-height: 1.15; font-weight: 650; letter-spacing: -.043em; font-variant-numeric: tabular-nums; white-space: nowrap; }
    .metric .metric-note { margin-top: .28rem; font-size: .74rem; color: var(--muted); line-height: 1.25; }
    .history-card { padding-bottom: .88rem; }
    .history-card .select { width: auto; min-height: 37px; padding-block: .32rem; font-size: .82rem; }
    .legend { display: flex; flex-wrap: wrap; align-items: center; gap: .18rem .62rem; margin-block: .8rem .18rem; }
    .legend button { display: inline-flex; align-items: center; gap: .35rem; padding: .22rem .24rem; border: 1px solid transparent; border-radius: 5px; background: transparent; color: var(--text); font-size: .77rem; cursor: pointer; white-space: nowrap; }
    .legend button:hover { background: var(--field); border-color: var(--line); }
    .legend button[aria-pressed="false"] { opacity: .5; text-decoration: line-through; }
    .legend-dot { background: var(--series-color); width: .45rem; height: .45rem; }
    .chart-box { position: relative; min-width: 0; }
    canvas { width: 100%; display: block; height: clamp(204px, 19vw, 255px); touch-action: pan-y; border-radius: 5px; }
    .io-plot canvas { height: 128px; }
    .plot-tip { position: absolute; z-index: 1; top: .18rem; pointer-events: none; padding: .45rem .6rem; color: var(--muted); background: #0e1723; border: 1px solid #43536b; border-radius: 8px; font-size: .75rem; min-width: 113px; max-width: 180px; box-shadow: 0 5px 18px #0005; }
    .plot-tip strong { color: var(--text); display: block; margin-bottom: .19rem; }
    .tip-row { display: flex; gap: .85rem; justify-content: space-between; }
    .timeline-head { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: .3rem .7rem; margin-top: .36rem; padding-top: .77rem; border-top: 1px solid var(--line); }
    .timeline-head .legend { margin: 0; }
    .notice { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: .75rem 1rem; border: 1px solid #78515b; border-left: 3px solid var(--danger); border-radius: 9px; background: #331f2a; padding: .8rem .92rem; margin-bottom: 1rem; }
    .notice h2 { color: #ffc2c8; }
    .notice p { color: #ecc4d0; font-size: .86rem; margin-top: .24rem; }
    .last-fault { border: 1px solid #544836; border-radius: 10px; background: #282520; color: var(--warm); }
    .last-fault summary { padding: .68rem .83rem; font-size: .87rem; font-weight: 650; cursor: pointer; }
    .last-fault p { color: #e5c9a3; font-size: .85rem; padding: 0 .92rem .78rem; }
    .settings-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); align-items: start; }
    .settings-grid .card { padding: 1.15rem 1.22rem; }
    .fields { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: .83rem .9rem; margin-top: .95rem; }
    .field { display: grid; align-content: start; gap: .29rem; color: var(--text); font-size: .89rem; min-width: 0; }
    .field em { color: var(--muted); font-style: normal; font-size: .81rem; }
    .field small { color: var(--muted); font-size: .75rem; }
    .span { grid-column: 1/-1; }
    .input, .select { width: 100%; min-width: 0; min-height: 43px; padding: .51rem .66rem; color: var(--text); background: var(--field); border: 1px solid #34465b; border-radius: 7px; }
    .input::placeholder { color: var(--dim); opacity: 1; }
    .input:disabled { opacity: .53; }
    .check { display: flex; gap: .52rem; align-items: center; color: var(--muted); font-size: .87rem; }
    .check input { flex: none; margin: 0; width: 1.05rem; height: 1.05rem; accent-color: var(--accent); }
    .form-actions { display: flex; align-items: center; gap: .5rem .85rem; flex-wrap: wrap; margin-top: .9rem; }
    .form-message { color: var(--tone, var(--muted)); font-size: .84rem; }
    .setup-notice { margin-top: .65rem; border: 1px solid #66543c; border-radius: 7px; background: #2d271f; padding: .56rem .68rem; color: var(--warm); font-size: .85rem; }
    .connection summary { display: list-item; cursor: pointer; font-weight: 650; }
    .connection .subtext { margin-top: .36rem; }
    #heater-output[data-on="1"] { background: #432d23; border-color: #c87c43; color: #ffbd85; }
    #fan-output[data-on="1"] { background: #193e3b; border-color: #449e89; color: #8ce5ce; }
    #heater-output[data-on="1"] > *, #fan-output[data-on="1"] > * { color: inherit; }
    .virtual-indicators { display: grid; grid-template-columns: repeat(3,minmax(0,1fr)); gap: .45rem; margin-top: .85rem; }
    .virtual-indicator { display: grid; justify-items: center; gap: .18rem; min-width: 0; text-align: center; color: var(--muted); }
    .virtual-indicator strong { font-size: .7rem; line-height: 1.25; overflow-wrap: anywhere; }
    .virtual-indicator output { color: var(--text); font-variant-numeric: tabular-nums; }
    .virtual-indicator small { font-size: .67rem; }
    .virtual-led { width: 1.14rem; height: 1.14rem; border-radius: 50%; background: #17232e; border: 1px solid #4b5b6a; }
    #sim-bed { --led: #e27178; --led-glow: #e2717844; }
    #sim-cavity { --led: #6eade4; --led-glow: #6eade444; }
    #sim-topcover { --led: #75bb83; --led-glow: #75bb8344; }
    .virtual-indicator[data-active="1"] .virtual-led { background: var(--led); border-color: var(--led); box-shadow: 0 0 .46rem var(--led-glow); }
    .virtual-log-title { margin-top: .85rem; font-size: .84rem; }
    .virtual-log { margin: .36rem 0 0; max-height: 9rem; overflow: auto; white-space: pre-wrap; overflow-wrap: anywhere;
      padding: .48rem .55rem; border-radius: 7px; background: #0b141e; border: 1px solid var(--line); color: #c3d6e1;
      font: .72rem/1.5 ui-monospace, Consolas, monospace; }
    .virtual-controls { display: flex; flex-wrap: wrap; gap: .48rem; margin-top: .85rem; }
    .virtual-controls .button[aria-pressed="true"] { color: var(--accent); background: #1c3841; border-color: #4c777d; }
    .restart-actions { border-top: 1px solid var(--line); margin-top: .9rem; padding-top: .9rem; }
    .config-info { margin-top: .8rem; color: var(--muted); font-size: .84rem; }
    .dialog { width: min(92vw, 515px); max-height: 86dvh; padding: 1.12rem; color: var(--text); background: var(--panel); border: 1px solid #475c73; border-radius: var(--radius); overflow: auto; }
    .dialog::backdrop { background: #070a11ba; }
    .dialog .card-head { flex-wrap: nowrap; }
    .dialog .subtext { margin-top: .65rem; }
    .dialog .close { min-height: 38px; padding-inline: .65rem; }
    .dialog .fields { margin-top: .75rem; }
    .dialog progress { width: 100%; accent-color: var(--accent); margin-top: .7rem; }
    .dialog .form-message { display: block; margin-top: .62rem; }
    .demo-grid { display: grid; grid-template-columns: repeat(2,minmax(0,1fr)); gap: .42rem; margin-top: .55rem; }
    .dialog h3 { margin-top: .8rem; }
    .toast { position: fixed; z-index: 5; inset: auto 1rem 1rem auto; max-width: min(390px, calc(100% - 2rem)); border: 1px solid #4c7b80; background: #16323b; border-radius: 8px; padding: .68rem .82rem; font-size: .87rem; color: #dbf7f3; box-shadow: 0 6px 18px #0006; }
    .skip { position: absolute; top: .5rem; left: 1rem; opacity: 0; pointer-events: none; z-index: 6; background: var(--field); padding: .5rem; }
    .skip:focus { opacity: 1; pointer-events: auto; }
    .visually-hidden { position: absolute; width: 1px; height: 1px; padding: 0; border: 0; clip-path: inset(50%); overflow: hidden; white-space: nowrap; }
    @media (max-width: 980px) {
      .metric { padding-inline: .57rem; }
      .metrics { gap: .42rem; }
      .status-grid > div { padding-inline: .64rem; }
    }
    @media (max-width: 760px) {
      .dashboard { grid-template-columns: 1fr; }
      .status-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }
      .status-grid > div:nth-child(4) { border-left: 0; }
      .status-grid > div:nth-child(n+4) { border-top: 1px solid var(--line); }
      .settings-grid { grid-template-columns: 1fr; }
      .thermal-card { display: grid; grid-template-columns: minmax(0, .95fr) minmax(0, 1.05fr); gap: .1rem .8rem; align-items: center; }
      .thermal-card > .card-head { grid-column: 1/-1; }
      .gauge { margin: .75rem auto -.2rem; }
      .temperature { font-size: clamp(2.9rem, 9vw, 4.3rem); }
      .metrics { gap: .55rem; }
    }
    @media (max-width: 525px) {
      .topbar { flex-wrap: wrap; min-height: unset; padding-block: .67rem .57rem; gap: .55rem; }
      .brand { flex: 1 1 auto; }
      .nav { order: 1; width: 100%; display: grid; grid-template-columns: repeat(2, 1fr); }
      .nav a { text-align: center; background: var(--panel); border-color: var(--line); }
      .button.demo-button { min-height: 38px; padding-inline: .55rem; font-size: .79rem; }
      .page-heading { align-items: center; }
      .live-label { display: none; }
      .status-grid { margin-bottom: .75rem; }
      .status-grid > div { padding: .54rem .55rem; }
      .status-grid dt { font-size: .75rem; }
      .status-grid dd { font-size: .8rem; }
      .thermal-card { display: block; }
      .gauge { width: min(100%, 218px); margin: .75rem auto -.34rem; }
      .temperature { font-size: 3.8rem; }
      .metrics { grid-template-columns: repeat(2, minmax(0,1fr)); }
      .metric { min-height: 90px; padding: .61rem .7rem; }
      .metric .metric-value { font-size: 1.48rem; }
      .card, .settings-grid .card { padding-inline: .88rem; }
      .fields { gap: .65rem .55rem; }
      .legend { column-gap: .35rem; }
      canvas { height: 220px; }
    }
    @media (prefers-reduced-motion: reduce) { *, *::before, *::after { scroll-behavior: auto !important; } }
  </style>
</head>
<body>
  <a class="skip" href="#main">Skip to main content</a>
  <header class="header">
    <div class="shell topbar">
      <a class="brand" href="#overview" aria-label="Snapmaker U1 Chamber Control overview">
        <span class="brand-mark" aria-hidden="true">U1</span>
        <span class="brand-text"><small>SNAPMAKER U1</small><strong>Chamber Control</strong></span>
      </a>
      <nav class="nav" aria-label="Controller pages">
        <a id="nav-overview" href="#overview" aria-current="page">Overview</a>
        <a id="nav-configuration" href="#configuration">Configuration</a>
      </nav>
      <button class="button demo-button" id="open-virtual" type="button"><span class="dot" style="color:inherit" aria-hidden="true"></span> <span id="virtual-shortcut">Virtual U1</span></button>
    </div>
  </header>

  <main class="shell" id="main">
    <section id="overview" aria-labelledby="overview-title">
      <div class="page-heading">
        <h1 id="overview-title">Chamber overview</h1>
        <p class="live-label"><span class="dot" aria-hidden="true"></span> <span id="live-status">Waiting for controller</span></p>
      </div>

      <dl class="status-grid">
        <div><dt>Controller</dt><dd>ESP32-S3</dd></div>
        <div><dt>Network</dt><dd id="network">—</dd></div>
        <div><dt>U1 link</dt><dd id="u1-link">—</dd></div>
        <div><dt>TopCover</dt><dd id="topcover">—</dd></div>
        <div><dt>NTC100</dt><dd id="ntc">—</dd></div>
        <div><dt>Local time</dt><dd><time id="clock">--:--:--</time></dd></div>
      </dl>

      <section class="notice" id="active-fault" aria-labelledby="fault-title" aria-live="assertive" hidden>
        <div><h2 id="fault-title">Safety alarm</h2><p id="fault-details"></p></div>
        <button class="button danger" type="button" id="fault-reset">Reset fault</button>
      </section>

      <div class="dashboard">
        <div class="stack">
          <section class="card thermal-card" aria-labelledby="thermal-title">
            <header class="card-head">
              <h2 id="thermal-title">Chamber temperature</h2>
              <span class="pill" id="operating-state"><span class="dot" aria-hidden="true"></span><span id="operating-label">—</span></span>
            </header>
            <div class="gauge" id="gauge" style="--sweep:0deg">
              <div class="gauge-arc" aria-hidden="true"></div>
              <div class="gauge-text">
                <div class="temperature"><span id="current-temp">—</span><small>°C</small></div>
                <div class="gauge-label">Current temperature</div>
                <div class="gauge-target">Target <span id="gauge-target">—</span></div>
              </div>
              <div class="gauge-scale" aria-hidden="true"><span>0 °C</span><span>65 °C</span></div>
            </div>
            <div>
              <div class="progress-title"><span>Chamber progress</span><strong id="progress-value">0%</strong></div>
              <div class="progress-track" id="chamber-progress" role="progressbar" aria-label="Chamber progress to target" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0">
                <div class="progress-fill" id="progress-fill" style="width:0"></div>
              </div>
              <div class="io-grid">
                <div class="io-cell" id="heater-output" title="Reported ESP32 heater control output"><span>Heater</span><strong id="heater-state">—</strong></div>
                <div class="io-cell" id="fan-output" title="Reported ESP32 fan control output"><span>Fan</span><strong id="fan-state">—</strong></div>
              </div>
              <button class="button primary power" id="power" type="button" data-running="false" disabled><span class="power-icon" aria-hidden="true"></span><span id="power-label">—</span></button>
              <p class="state-description" id="state-description">—</p>
            </div>
          </section>

          <section class="card" aria-labelledby="control-title">
            <header class="card-head"><h2 id="control-title">Temperature control</h2><span class="pill" id="bed-assist">—</span></header>
            <div class="segmented" role="group" aria-label="Heating mode">
              <button class="button" type="button" id="manual" aria-pressed="false">MANUAL</button>
              <button class="button" type="button" id="preheat" aria-pressed="false">PREHEAT</button>
            </div>
            <div class="mode-state" id="preheat-indicator" hidden><span>Preheat status</span><strong class="tone" id="preheat-status" data-tone="warm">Warming</strong></div>
            <div class="stepper" role="group" aria-label="Target temperature adjustment">
              <button class="button" type="button" id="minus" aria-label="Decrease target temperature by 1 degree">−</button>
              <output id="quick-target" aria-label="Target temperature">—</output>
              <button class="button" type="button" id="plus" aria-label="Increase target temperature by 1 degree">+</button>
            </div>
            <div class="presets" role="group" aria-label="Material temperature presets">
              <button class="button" type="button" data-target="50"><span>50 °C</span><small>PETG</small></button>
              <button class="button" type="button" data-target="55"><span>55 °C</span><small>ABS / ASA</small></button>
              <button class="button" type="button" data-target="60"><span>60 °C</span><small>PC / Nylon</small></button>
            </div>
          </section>
        </div>

        <div class="stack">
          <dl class="metrics" aria-label="Temperature readings">
            <div class="metric" data-series="target"><dt class="metric-label">Target</dt><dd class="metric-value" id="metric-target" data-tone="warm">—</dd><dd class="metric-note">Chamber setpoint</dd></div>
            <div class="metric" data-series="delta"><dt class="metric-label">To target</dt><dd class="metric-value" id="metric-delta" data-tone="accent">—</dd><dd class="metric-note">Chamber − target</dd></div>
            <div class="metric" data-series="cavity"><dt class="metric-label">U1 cavity</dt><dd class="metric-value" id="metric-cavity" style="color:var(--purple)">—</dd><dd class="metric-note" id="cavity-note">Printer sensor</dd></div>
            <div class="metric" data-series="bed"><dt class="metric-label">Printer bed</dt><dd class="metric-value" id="metric-bed" style="color:var(--bed)">—</dd><dd class="metric-note" id="bed-note">—</dd></div>
          </dl>

          <section class="card history-card" aria-labelledby="history-title">
            <header class="card-head">
              <div><h2 id="history-title">Temperature history</h2><p class="subtext">Sampled every 5 seconds</p></div>
              <label><span class="visually-hidden">History range</span><select class="select" id="history-range"><option value="15">Last 15 min</option><option value="30">Last 30 min</option><option value="60" selected>Last 60 min</option></select></label>
            </header>
            <div id="temp-legend" class="legend" role="group" aria-label="Temperature series"></div>
            <div class="chart-box">
              <canvas id="temp-chart" tabindex="0" aria-label="Temperature history in degrees Celsius. Use the left and right arrow keys to inspect samples."></canvas>
              <div class="plot-tip" id="temp-tip" hidden></div>
            </div>

            <div class="timeline-head">
              <h3>Heater / fan activity</h3>
              <div id="io-legend" class="legend" role="group" aria-label="Activity series"></div>
            </div>
            <div class="chart-box io-plot">
              <canvas id="io-chart" tabindex="0" aria-label="Heater and fan on/off history. Use the left and right arrow keys to inspect samples."></canvas>
              <div class="plot-tip" id="io-tip" hidden></div>
            </div>
            <output class="visually-hidden" id="chart-readout" aria-live="polite"></output>
          </section>

          <details class="last-fault" id="last-fault" hidden>
            <summary id="last-fault-title">—</summary>
            <p id="last-fault-details"></p>
          </details>
        </div>
      </div>
    </section>

    <section id="configuration" aria-labelledby="configuration-title" hidden>
      <div class="page-heading">
        <h1 id="configuration-title">Configuration</h1>
        <p class="live-label"><span class="dot" aria-hidden="true"></span> Controller settings</p>
      </div>
      <div class="settings-grid">
          <section class="card" aria-labelledby="advanced-title">
            <header class="card-head"><div><h2 id="advanced-title">Advanced settings</h2><p class="subtext">Thermal management</p></div></header>
            <form id="thermal-form" action="/cfg" method="get">
              <div class="fields">
                <label class="field"><span>Hysteresis <em>°C</em></span><input class="input" name="hyst" type="number" min=".1" max="10" step="any"><small>0.1–10 °C</small></label>
                <label class="field"><span>Temperature limit <em>°C</em></span><input class="input" name="max" type="number" min="1" max="65" step="any"><small>1–65 °C</small></label>
                <label class="field"><span>Pre-fan <em>s</em></span><input class="input" name="pre" type="number" min="0" max="600" step="1"><small>0–600 s</small></label>
                <label class="field"><span>Post-fan <em>s</em></span><input class="input" name="post" type="number" min="0" max="600" step="1"><small>0–600 s</small></label>
              </div>
              <div class="form-actions"><button class="button primary" type="submit">Save settings</button><output class="form-message" id="thermal-message" aria-live="polite"></output></div>
            </form>
          </section>

          <details class="card connection" id="connection-panel" open>
            <summary>Controller connection</summary>
            <p class="subtext">Wi-Fi and U1 Moonraker address. TopCover detection is provided by U1.</p>
            <p id="setup-notice" class="setup-notice" hidden>Configuration mode · Connect to U1-Chamber-Setup and open 192.168.4.1</p>
            <form id="connection-form" action="/devicecfg" method="post">
              <div class="fields">
                <label class="field"><span>Wi-Fi network</span><input class="input" name="wifiSsid" type="text" maxlength="32" autocomplete="off" required></label>
                <label class="field"><span>U1 IP address</span><input class="input" name="printerIp" type="text" inputmode="decimal" maxlength="15" placeholder="192.168.x.x" autocomplete="off" required></label>
                <label class="field span"><span>Wi-Fi password</span><input class="input" name="wifiPass" type="password" maxlength="64" autocomplete="off" placeholder="Leave blank to keep saved"></label>
                <label class="check span"><input name="clearWifiPass" type="checkbox" value="1"> Use open Wi-Fi network (clear saved password)</label>
              </div>
              <div class="form-actions"><button class="button primary" type="submit">Save connection settings</button><output class="form-message" id="connection-message" aria-live="polite"></output></div>
            </form>
          </details>

          <section class="card" aria-labelledby="firmware-title">
            <header class="card-head">
              <div><h2 id="firmware-title">Firmware update</h2><p class="subtext">Wi-Fi update and access password</p></div>
              <span class="pill" id="ota-status">—</span>
            </header>
            <form id="ota-password-form" action="/otapassword" method="post">
              <div class="fields">
                <label class="field span" id="current-ota-password-field" hidden><span>Current firmware update password</span><input class="input" name="currentOtaPassword" type="password" autocomplete="current-password" disabled></label>
                <label class="field span"><span>Firmware update password</span><input class="input" name="otaPassword" type="password" minlength="8" maxlength="64" autocomplete="off" placeholder="At least 8 characters" required></label>
              </div>
              <div class="form-actions"><button class="button primary" type="submit">Save update password</button><button class="button" type="button" id="open-ota">Firmware update (Wi-Fi)</button></div>
              <output class="form-message" id="ota-message" aria-live="polite"></output>
            </form>
          </section>

          <section class="card" aria-labelledby="virtual-title">
            <header class="card-head">
              <div><h2 id="virtual-title" tabindex="-1">Virtual U1</h2><p class="subtext">Test TopCover detection and print handoff.</p></div>
              <span class="pill" id="virtual-status">Off</span>
            </header>
            <div class="virtual-indicators" aria-label="Last simulated commands">
              <div class="virtual-indicator" id="sim-bed"><span class="virtual-led" aria-hidden="true"></span><strong>BED ASSIST</strong><output id="sim-bed-value">—</output><small id="sim-bed-activity">Inactive</small></div>
              <div class="virtual-indicator" id="sim-cavity"><span class="virtual-led" aria-hidden="true"></span><strong>CAVITY FAN</strong><output id="sim-cavity-value">—</output><small id="sim-cavity-activity">Inactive</small></div>
              <div class="virtual-indicator" id="sim-topcover"><span class="virtual-led" aria-hidden="true"></span><strong>TOPCOVER FAN</strong><output id="sim-topcover-value">—</output><small id="sim-topcover-activity">Inactive</small></div>
            </div>
            <div class="virtual-controls" id="virtual-enable">
              <button class="button" type="button" id="virtual-on">Enable Virtual U1</button>
            </div>
            <div class="virtual-controls" id="virtual-actions" hidden>
              <button class="button" type="button" data-virtual="standby-cover">Standby + cover</button>
              <button class="button" type="button" data-virtual="standby-open">Standby without cover</button>
              <button class="button" type="button" data-virtual="printing">Simulate printing</button>
              <button class="button" type="button" data-virtual="complete">Simulate complete</button>
              <button class="button" type="button" data-virtual="cancelled">Simulate cancelled</button>
              <button class="button danger" type="button" id="virtual-off">Exit Virtual U1</button>
            </div>
            <p class="config-info" id="virtual-print" hidden></p>
            <h3 class="virtual-log-title" id="virtual-log-title">Simulation Event Log</h3>
            <p class="subtext" id="virtual-log-status">Waiting for diagnostics</p>
            <pre class="virtual-log" id="virtual-log" role="region" aria-labelledby="virtual-log-title" tabindex="0">No simulation events yet.</pre>
            <div class="form-actions restart-actions">
              <button class="button" type="button" id="restart">Restart controller</button>
              <button class="button danger" type="button" id="factory-reset">Factory reset</button>
            </div>
          </section>
      </div>
    </section>
  </main>

  <dialog class="dialog" id="confirm-dialog" aria-labelledby="confirm-title">
    <header class="card-head"><h2 id="confirm-title"></h2></header>
    <p class="subtext" id="confirm-text"></p>
    <label class="field" id="confirm-ota-password-field" hidden><span>Current firmware update password</span><input class="input" id="confirm-ota-password" type="password" autocomplete="current-password"></label>
    <div class="form-actions"><button class="button" type="button" data-close="confirm-dialog" autofocus>Cancel</button><button class="button" type="button" id="confirm-action"></button></div>
  </dialog>

  <dialog class="dialog" id="ota-dialog" aria-labelledby="ota-title">
    <header class="card-head"><h2 id="ota-title">Firmware update (Wi-Fi)</h2><button class="button close" type="button" data-close="ota-dialog">Close</button></header>
    <p class="subtext">The heater will be locked OFF before the firmware is written. Select the regular Arduino <code>.ino.bin</code> file.</p>
    <p class="subtext">Use username <code>chamber</code> and the saved update password.</p>
    <form id="ota-upload-form">
      <div class="fields">
        <label class="field"><span>Username</span><input class="input" name="username" value="chamber" maxlength="32" autocomplete="off" required></label>
        <label class="field"><span>Update password</span><input class="input" name="password" type="password" maxlength="64" autocomplete="off" required></label>
        <label class="field span"><span>Firmware file (.bin)</span><input class="input" name="firmware" type="file" accept=".bin" required></label>
      </div>
      <progress id="upload-progress" aria-label="Firmware upload progress" max="100" value="0" hidden></progress>
      <output class="form-message" id="upload-message" aria-live="polite"></output>
      <div class="form-actions"><button class="button primary" type="submit" id="upload-button">Upload firmware</button></div>
    </form>
  </dialog>
  <div class="toast" id="toast" role="status" hidden></div>

  <script>
    'use strict';
    const byId = id => document.getElementById(id);
    const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
    const degrees = value => Number.isFinite(value) ? value.toFixed(1) + ' °C' : '—';
    const signedDegrees = value => (value < 0 ? '−' : value > 0 ? '+' : '') + degrees(Math.abs(value));
    const clockFormat = new Intl.DateTimeFormat('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    const axisTime = new Intl.DateTimeFormat('en-GB', { hour: '2-digit', minute: '2-digit' });
    const safetyLimit = 65;
    const historyCapacity = 720;
    const refreshInterval = 5000;
    const phases = [
      ['Idle', 'Chamber heating is off.', 'muted'],
      ['Pre-fan', 'Preparing airflow before heating.', 'warm'],
      ['Heating active', 'Heater is actively warming the chamber.', 'warm'],
      ['Cooling', 'Post-heating cooling cycle.', 'accent']
    ];
    const faultLabels = ['', 'NTC', 'Overtemperature', 'Thermal runaway', 'Safety system'];
    const snapshotStart = '<meta id="controller-view"';
    const snapshotEnd = '>\n';
    let view = byId('controller-view').dataset;
    let telemetry = null;
    let telemetryFresh = false;
    let samples = [];
    let lastSequence = null;
    let pollActive = false;
    let actionActive = false;
    let actionGeneration = 0;
    let awaitRestart = false;
    let toastTimer;
    let confirmation;

    function setText(id, value) {
      const element = byId(id);
      if (element.textContent !== value) element.textContent = value;
    }

    function setTone(id, value, tone) {
      setText(id, value);
      byId(id).dataset.tone = tone;
    }

    function toast(text) {
      clearTimeout(toastTimer);
      setText('toast', text);
      byId('toast').hidden = false;
      toastTimer = setTimeout(() => { byId('toast').hidden = true; }, 4500);
    }

    function formMessage(id, text, tone = 'ok') {
      setTone(id, text, tone);
    }

    function faultDetails(data, prefix) {
      if (!data[prefix + 'Valid']) return 'No diagnostic data';
      const field = name => data[prefix + name];
      const target = ' · Target ' + degrees(field('Target'));
      const reason = field('Reason');
      if (reason === 1) return 'Last valid temperature ' + degrees(field('LastValidTemp')) + target;
      if (reason === 2) return 'Temperature ' + degrees(field('Temp')) + ' · Limit 65.0 °C' + target;
      if (reason === 3 && field('RunawayValid')) {
        return 'Start ' + degrees(field('RunawayStart')) + ' · End ' + degrees(field('RunawayEnd')) +
          ' · Net rise ' + signedDegrees(field('RunawayRise')) + ' / ' + Math.round(field('RunawayDurationMs') / 1000) +
          ' s · Min/max ' + degrees(field('RunawayMin')) + ' / ' + degrees(field('RunawayMax')) +
          ' · Required rise ' + signedDegrees(field('RunawayRequiredRise')) + target;
      }
      return 'Target ' + degrees(field('Target')) + ' · Uptime ' + Math.round(field('AtMs') / 1000) + ' s';
    }

    function renderOutputs(data) {
      for (const [key, tone] of [["heater", "warm"], ["fan", "ok"]]) {
        const on = Boolean(data[key]);
        byId(key + "-output").dataset.on = on ? "1" : "0";
        setTone(key + "-state", on ? "ON" : "OFF", on ? tone : "muted");
      }
    }

    function renderStatus() {
      setText('live-status', awaitRestart ? 'Controller restarting' : telemetryFresh ? 'Live data' :
        telemetry ? 'Telemetry unavailable' : 'Waiting for controller');
      setText('network', view.portal === '1' ? 'Setup AP' : 'Wi-Fi');
      const data = telemetry;
      byId('power').disabled = actionActive || awaitRestart || !data || Boolean(data.fault);
      byId('preheat').disabled = actionActive || awaitRestart || Boolean(data?.fault) || view.print === 'printing';
      byId('manual').disabled = actionActive || awaitRestart;
      if (!data) return;

      const [label, description, tone] = data.fault ? ['Safety lockout', 'Local heater is locked off.', 'bad'] :
        phases[data.state] || ['Unknown', 'Controller state unavailable.', 'muted'];
      setText('operating-label', label);
      byId('operating-state').dataset.tone = tone;
      setText('state-description', description);
      setTone('u1-link', data.u1Online ? 'Online' : 'Offline', data.u1Online ? 'ok' : 'bad');
      setTone('topcover', data.topCoverValid ? (data.topCoverDetected ? 'Connected' : 'Not detected') : 'Unknown',
        !data.topCoverValid ? 'warm' : data.topCoverDetected ? 'ok' : 'muted');
      setTone('ntc', data.ntcOk ? 'OK' : 'Fault', data.ntcOk ? 'ok' : 'bad');
      setText('current-temp', data.temp.toFixed(1));
      for (const id of ['gauge-target', 'quick-target', 'metric-target']) setText(id, degrees(data.target));
      byId('gauge').style.setProperty('--sweep', (clamp(data.temp / safetyLimit, 0, 1) * 270) + 'deg');
      const progress = data.target > 0 ? Math.round(clamp(data.temp / data.target, 0, 1) * 100) : 0;
      setText('progress-value', progress + '%');
      byId('chamber-progress').setAttribute('aria-valuenow', progress);
      byId('progress-fill').style.width = progress + '%';
      renderOutputs(data);
      byId('power').dataset.running = String(Boolean(data.master));
      setText('power-label', data.master ? 'STOP' : 'START');
      byId('manual').setAttribute('aria-pressed', String(!data.preheatMode));
      byId('preheat').setAttribute('aria-pressed', String(Boolean(data.preheatMode)));
      byId('preheat-indicator').hidden = !data.preheatMode;
      setTone('preheat-status', data.preheatReady ? 'Ready' : 'Warming', data.preheatReady ? 'ok' : 'warm');
      const assist = view.assist;
      setTone('bed-assist', { on: 'Bed assist on', pending: 'Bed assist pending', off: 'Bed assist off' }[assist],
        { on: 'accent', pending: 'warm', off: 'muted' }[assist]);
      setText('metric-delta', signedDegrees(data.temp - data.target));
      setText('metric-cavity', data.printerCavityValid ? degrees(data.printerCavity) : '—');
      setText('cavity-note', data.printerCavityValid ? 'Printer sensor' : 'Telemetry unavailable');
      setText('metric-bed', data.printerBedValid ? degrees(data.printerBed) : '—');
      const bedStatus = data.printerBedHeating ? 'Heating' : 'Idle';
      const bedTarget = data.printerBedTargetValid && data.printerBedTarget > 0 ?
        ' · ' + data.printerBedTarget + ' °C target' : '';
      setText('bed-note', data.printerBedValid ? bedStatus + bedTarget : 'Telemetry unavailable');

      byId('active-fault').hidden = !data.fault;
      if (data.fault) {
        setText('fault-title', 'Safety alarm · ' + (faultLabels[data.faultReason] || 'Safety system'));
        setText('fault-details', faultDetails(data, 'faultDetail'));
      }
      byId('last-fault').hidden = !data.lastFaultValid;
      if (data.lastFaultValid) {
        setText('last-fault-title', 'Last fault · ' + (faultLabels[data.lastFaultReason] || 'Safety system'));
        setText('last-fault-details', faultDetails(data, 'lastFault'));
      }
    }

    function renderConfiguration() {
      const configured = view.ota === '1';
      const currentPassword = byId('ota-password-form').elements.currentOtaPassword;
      byId('current-ota-password-field').hidden = !configured;
      currentPassword.disabled = !configured;
      currentPassword.required = configured;
      setTone('ota-status', configured ? 'Password configured' : 'Password required', configured ? 'ok' : 'warm');
      byId('setup-notice').hidden = view.portal !== '1';
      const virtual = view.virtual === '1';
      setTone('virtual-status', virtual ? 'On' : 'Off', virtual ? 'accent' : 'muted');
      setTone('virtual-shortcut', virtual ? 'Virtual U1 on' : 'Virtual U1', virtual ? 'accent' : 'warm');
      byId('virtual-enable').hidden = virtual;
      byId('virtual-actions').hidden = !virtual;
      byId('virtual-print').hidden = !virtual;
      if (virtual) {
        const print = view.virtualPrint;
        const cover = view.virtualCover === '1';
        setText('virtual-print', 'U1 print state · ' + print.charAt(0).toUpperCase() + print.slice(1) +
          ' · TopCover ' + (cover ? 'connected' : 'not detected'));
        const choice = print === 'standby' ? (cover ? 'standby-cover' : 'standby-open') : print;
        for (const button of document.querySelectorAll('[data-virtual]')) {
          button.setAttribute('aria-pressed', String(button.dataset.virtual === choice));
        }
      }
    }

    function refillThermalForm() {
      const fields = byId('thermal-form').elements;
      for (const key of ['hyst', 'pre', 'post', 'max']) fields.namedItem(key).value = Number(view[key]);
    }

    function refillConnectionForm() {
      const fields = byId('connection-form').elements;
      fields.wifiSsid.value = view.ssid;
      fields.printerIp.value = view.ip;
      fields.wifiPass.value = '';
      fields.clearWifiPass.checked = false;
      fields.wifiPass.disabled = false;
    }

    function applyView(data) {
      view = data;
      renderConfiguration();
      renderStatus();
    }

    function snapshotFrom(text) {
      const start = text.indexOf(snapshotStart);
      const end = start >= 0 ? text.indexOf(snapshotEnd, start) : -1;
      if (end < 0) return null;
      const fragment = document.createElement('template');
      fragment.innerHTML = text.slice(start, end + 1);
      return fragment.content.firstElementChild.dataset;
    }

    async function readView(response) {
      if (!response.ok) throw new Error(await responseMessage(response));
      if (!response.body?.getReader) {
        const data = snapshotFrom(await response.text());
        if (data) return data;
      } else {
        // The live view values are sent before the flash-resident page assets.
        // Read only the beginning of this existing HTML response.
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let text = '';
        try {
          while (text.length < 8192) {
            const { done, value } = await reader.read();
            if (done) break;
            text += decoder.decode(value, { stream: true });
            const data = snapshotFrom(text);
            if (data) return data;
          }
        } finally {
          reader.cancel().catch(() => {});
        }
      }
      throw new Error('Controller page could not be read.');
    }

    async function responseMessage(response) {
      const body = await response.text();
      const message = new DOMParser().parseFromString(body, 'text/html').body.textContent.trim();
      return message || 'Controller returned HTTP ' + response.status + '.';
    }

    async function loadTelemetry(generation) {
      try {
        const response = await fetch('/temp.json', { cache: 'no-store' });
        if (!response.ok) throw new Error('Telemetry request failed.');
        const data = await response.json();
        if (!Number.isFinite(data.temp) || !Number.isFinite(data.target)) throw new Error('Invalid temperature data.');
        if (generation !== actionGeneration) return;
        telemetry = data;
        telemetryFresh = true;
      } catch {
        if (generation === actionGeneration) telemetryFresh = false;
      }
      if (generation === actionGeneration) renderStatus();
    }

    async function loadHistory(generation) {
      const path = lastSequence === null ? '/history.json' : '/history.json?after=' + lastSequence;
      const response = await fetch(path, { cache: 'no-store' });
      if (!response.ok) throw new Error('History request failed.');
      const data = await response.json();
      if (generation !== actionGeneration || !Array.isArray(data.samples) ||
          !Number.isInteger(data.now) || !Number.isInteger(data.lastSeq)) return;
      const receivedAt = Date.now();
      if (lastSequence === null || data.reset) samples = [];
      for (const [uptime, chamber, target, cavity, bed, flags] of data.samples) {
        samples.push({ uptime, chamber, target, cavity, bed, heater: Boolean(flags & 1), fan: Boolean(flags & 2) });
      }
      if (samples.length > historyCapacity) samples.splice(0, samples.length - historyCapacity);
      for (const sample of samples) sample.at = receivedAt - ((data.now - sample.uptime) >>> 0);
      lastSequence = data.lastSeq >>> 0;
      redrawCharts();
    }

    function redrawCharts() {
      if (window.drawChamberCharts) window.drawChamberCharts();
    }

    let virtualLogRevision;

    function virtualLogTime(ms) {
      const seconds = Math.floor(ms / 1000);
      const two = value => String(value).padStart(2, "0");
      return "+" + two(Math.floor(seconds / 3600)) + ":" + two(Math.floor(seconds / 60) % 60) + ":" + two(seconds % 60);
    }

    // Wire order matches VirtualEvent in chamber_virtual_u1.h.
    const virtualLogLabels = [
      "SYSTEM · Virtual U1 enabled", "SYSTEM · Virtual U1 disabled",
      "RX SIM · STANDBY", "RX SIM · PRINTING", "RX SIM · COMPLETE", "RX SIM · CANCELLED",
      value => "RX SIM · TopCover " + (value ? "connected" : "not detected"),
      value => "TX · BED TARGET " + value + " °C",
      value => "TX · BED TARGET " + value + " °C (renewal)",
      value => "TX · CAVITY FAN " + value + "%",
      value => "TX · TOPCOVER FAN " + value + "%",
      "RX SIM · OK", "SYSTEM · BED ASSIST renewal queued", "SYSTEM · BED ASSIST renewal cancelled",
      "SYSTEM · U1 control transferred to print", "SYSTEM · PREHEAT → MANUAL", "SYSTEM · PREHEAT selected",
      "SYSTEM · PRE-FAN started", value => "SYSTEM · Local HEATER output " + (value ? "ON" : "OFF"),
      value => "SYSTEM · Local FAN output " + (value ? "ON" : "OFF"),
      "SYSTEM · Print ended", "SYSTEM · Print cancelled"
    ];

    function renderVirtualDiagnostics(data) {
      const outputs = ["bed", "cavity", "topcover"];
      for (const [index, name] of outputs.entries()) {
        const value = data?.targets[index];
        const active = Boolean(data?.active[index]);
        byId("sim-" + name).dataset.active = active ? "1" : "0";
        if (data) setText("sim-" + name + "-value", value >= 0 ? value + (index === 0 ? " °C" : "%") : "—");
        setText("sim-" + name + "-activity", active ? "Chamber active" : "Inactive");
      }
      if (!data) {
        setText("virtual-log-status", "Diagnostics unavailable");
        return;
      }
      const renewal = data.renew < 0 ? "Renewal inactive" : data.pending ? "Renewal queued" :
        "Next renewal in " + Math.ceil(data.renew / 1000) + " s";
      setText("virtual-log-status", "Simulated commands: " + data.commands + " · " + renewal);
      if (virtualLogRevision === data.revision) return;
      virtualLogRevision = data.revision;
      const log = byId("virtual-log");
      const follow = log.scrollHeight - log.clientHeight - log.scrollTop <= 4;
      const lines = data.events.map(([at, kind, value]) => {
        const label = virtualLogLabels[kind];
        return virtualLogTime(at) + " " + (typeof label === "function" ? label(value) : label);
      });
      log.textContent = lines.join("\n") || "No simulation events yet.";
      if (follow) log.scrollTop = log.scrollHeight;
    }

    async function loadVirtualDiagnostics(generation) {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 1500);
      try {
        const response = await fetch("/virtualu1.json", { cache: "no-store", signal: controller.signal });
        if (!response.ok) throw new Error("Diagnostics request failed.");
        const data = await response.json();
        if (generation === actionGeneration) renderVirtualDiagnostics(data);
      } catch {
        if (generation === actionGeneration) renderVirtualDiagnostics(null);
      } finally {
        clearTimeout(timeout);
      }
    }

    async function poll() {
      if (pollActive || actionActive) return;
      pollActive = true;
      const generation = actionGeneration;
      try {
        await loadTelemetry(generation);
        if (generation !== actionGeneration) return;
        try {
          const data = await readView(await fetch('/', { cache: 'no-store' }));
          if (generation !== actionGeneration) return;
          if (awaitRestart && telemetryFresh) {
            awaitRestart = false;
            samples = [];
            lastSequence = null;
            applyView(data);
            refillThermalForm();
            refillConnectionForm();
          } else {
            applyView(data);
          }
        } catch {
          // The dedicated telemetry and history routes remain usable.
        }
        if (generation === actionGeneration) await loadHistory(generation).catch(() => {});
        if (generation === actionGeneration && !awaitRestart && !byId("configuration").hidden) {
          await loadVirtualDiagnostics(generation);
        }
      } finally {
        pollActive = false;
      }
    }

    function openConfirmation(title, message, label, dangerous, action, needsOtaPassword = false) {
      const password = byId('confirm-ota-password');
      password.value = '';
      password.required = needsOtaPassword;
      byId('confirm-ota-password-field').hidden = !needsOtaPassword;
      setText('confirm-title', title);
      setText('confirm-text', message);
      const button = byId('confirm-action');
      button.textContent = label;
      button.className = dangerous ? 'button danger' : 'button primary';
      confirmation = action;
      byId('confirm-dialog').showModal();
    }

    async function runAction(action, messageId) {
      if (actionActive || awaitRestart) return;
      actionActive = true;
      const generation = ++actionGeneration;
      renderStatus();
      try {
        await action();
        if (!awaitRestart) await loadTelemetry(generation);
      } catch (error) {
        const message = error instanceof TypeError ? 'Could not reach the controller.' : error.message;
        if (messageId) formMessage(messageId, message, 'bad');
        else toast(message);
      } finally {
        actionActive = false;
        renderStatus();
      }
    }

    async function controllerAction(path, options = {}) {
      const response = await fetch(path, { cache: 'no-store', ...options });
      if (!response.ok) throw new Error(await responseMessage(response));
      if (response.redirected) {
        const data = await readView(response);
        applyView(data);
        return data;
      }
      return response;
    }

    function startRestartWait(message) {
      awaitRestart = true;
      telemetryFresh = false;
      toast(message);
      renderStatus();
    }

    function showPage() {
      const hash = window.location.hash;
      if (hash === '#main') return;
      const page = hash === '#configuration' ? 'configuration' : 'overview';
      for (const id of ['overview', 'configuration']) {
        byId(id).hidden = id !== page;
        const nav = byId('nav-' + id);
        if (id === page) nav.setAttribute('aria-current', 'page');
        else nav.removeAttribute('aria-current');
      }
      window.scrollTo(0, 0);
      requestAnimationFrame(redrawCharts);
    }

    byId('power').addEventListener('click', () => {
      const label = byId('power-label').textContent;
      if (label !== 'START' && label !== 'STOP') return;
      const path = label === 'STOP' ? '/stop' : '/start';
      runAction(() => controllerAction(path, { method: 'POST' }));
    });
    for (const [id, path] of Object.entries({ minus: '/adjust?d=-1', plus: '/adjust?d=1', 'fault-reset': '/faultreset' })) {
      byId(id).addEventListener('click', () => runAction(() => controllerAction(path)));
    }
    for (const mode of ['manual', 'preheat']) {
      byId(mode).addEventListener('click', () => runAction(async () => {
        await controllerAction('/mode?m=' + mode + '&ajax=1');
        try {
          applyView(await readView(await fetch('/', { cache: 'no-store' })));
        } catch {
          // Always reconcile the mode and READY state with /temp.json.
        }
      }));
    }
    for (const button of document.querySelectorAll('[data-target]')) {
      button.addEventListener('click', () => runAction(() => controllerAction('/set?st=' + button.dataset.target)));
    }
    byId('open-virtual').addEventListener('click', () => {
      if (location.hash !== '#configuration') location.hash = 'configuration';
      setTimeout(() => byId('virtual-title').focus(), 0);
    });
    for (const button of document.querySelectorAll('[data-close]')) {
      button.addEventListener('click', () => byId(button.dataset.close).close());
    }
    byId('confirm-action').addEventListener('click', () => {
      const password = byId('confirm-ota-password');
      if (password.required && !password.reportValidity()) return;
      const currentOtaPassword = password.value;
      const action = confirmation;
      byId('confirm-dialog').close();
      action(currentOtaPassword);
    });
    byId('confirm-dialog').addEventListener('close', () => { byId('confirm-ota-password').value = ''; });

    function selectVirtual(choice) {
      const standby = choice.startsWith('standby');
      const query = new URLSearchParams({ mode: 'on', state: standby ? 'standby' : choice,
        topcover: choice === 'standby-open' ? '0' : '1' });
      return controllerAction('/virtualu1?' + query);
    }

    byId('virtual-on').addEventListener('click', () => openConfirmation('Enable Virtual U1?',
      'Real U1 HTTP and G-code commands are bypassed until you exit test mode.', 'Enable Virtual U1', false,
      () => runAction(() => selectVirtual('standby-cover'))));
    for (const button of document.querySelectorAll('[data-virtual]')) {
      button.addEventListener('click', () => runAction(() => selectVirtual(button.dataset.virtual)));
    }
    byId('virtual-off').addEventListener('click', () => runAction(() => controllerAction('/virtualu1?mode=off')));
    byId('restart').addEventListener('click', () => openConfirmation('Restart controller?',
      'Heating will be safely stopped before restarting.', 'Restart controller', false,
      () => runAction(async () => startRestartWait(await responseMessage(await controllerAction('/restart'))))));
    byId('factory-reset').addEventListener('click', () => openConfirmation('Factory reset?',
      'Delete Wi-Fi, U1 IP and all saved controller settings?', 'Factory reset', true,
      currentOtaPassword => runAction(async () => {
        const body = new URLSearchParams({ confirm: '1', currentOtaPassword });
        const response = await controllerAction('/factoryreset', { method: 'POST', body });
        startRestartWait(await responseMessage(response));
      }), view.ota === '1'));


    byId('thermal-form').addEventListener('submit', event => {
      event.preventDefault();
      const args = new URLSearchParams(new FormData(event.currentTarget));
      runAction(async () => {
        await controllerAction('/cfg?' + args);
        refillThermalForm();
        formMessage('thermal-message', 'Settings saved.');
      }, 'thermal-message');
    });
    const connectionFields = byId('connection-form').elements;
    connectionFields.clearWifiPass.addEventListener('change', () => {
      connectionFields.wifiPass.disabled = connectionFields.clearWifiPass.checked;
    });
    byId('connection-form').addEventListener('submit', event => {
      event.preventDefault();
      const args = new URLSearchParams(new FormData(event.currentTarget));
      runAction(async () => {
        const result = await controllerAction('/devicecfg', { method: 'POST', body: args });
        if (!(result instanceof Response)) {
          formMessage('connection-message', result.message, 'bad');
          return;
        }
        connectionFields.wifiPass.value = '';
        const message = await responseMessage(result);
        formMessage('connection-message', message);
        startRestartWait(message);
      }, 'connection-message');
    });
    byId('ota-password-form').addEventListener('submit', event => {
      event.preventDefault();
      const form = event.currentTarget;
      const args = new URLSearchParams(new FormData(form));
      runAction(async () => {
        const data = await controllerAction('/otapassword', { method: 'POST', body: args });
        form.reset();
        formMessage('ota-message', data.message || 'Firmware update password saved.',
          data.message?.includes('at least') ? 'bad' : 'ok');
      }, 'ota-message');
    });

    byId('open-ota').addEventListener('click', () => {
      if (actionActive || awaitRestart) return;
      if (view.ota !== '1') {
        formMessage('ota-message', 'Set a firmware update password first.', 'warm');
        return;
      }
      byId('ota-upload-form').reset();
      byId('upload-progress').hidden = true;
      byId('upload-progress').value = 0;
      formMessage('upload-message', '');
      byId('ota-dialog').showModal();
    });

    function uploadFirmware(file, authorization) {
      return new Promise((resolve, reject) => {
        const request = new XMLHttpRequest();
        request.open('POST', '/update');
        request.setRequestHeader('Authorization', authorization);
        const progress = byId('upload-progress');
        request.upload.addEventListener('progress', event => {
          if (event.lengthComputable) progress.value = 100 * event.loaded / event.total;
        });
        request.addEventListener('load', () => {
          const message = new DOMParser().parseFromString(request.responseText, 'text/html').body.textContent.trim();
          if (request.status >= 200 && request.status < 300) resolve(message);
          else reject(new Error(message || 'Firmware update failed (HTTP ' + request.status + ').'));
        });
        request.addEventListener('error', () => reject(new Error('Connection lost during firmware update.')));
        request.addEventListener('abort', () => reject(new Error('Firmware upload aborted.')));
        const body = new FormData();
        body.append('firmware', file);
        request.send(body);
      });
    }

    byId('ota-upload-form').addEventListener('submit', event => {
      event.preventDefault();
      const fields = event.currentTarget.elements;
      const file = fields.firmware.files[0];
      if (!file || file.size === 0 || !file.name.toLowerCase().endsWith('.bin')) {
        formMessage('upload-message', 'Select a non-empty .bin file.', 'bad');
        return;
      }
      const credentials = new TextEncoder().encode(fields.username.value + ':' + fields.password.value);
      const authorization = 'Basic ' + btoa(String.fromCharCode(...credentials));
      fields.password.value = '';
      runAction(async () => {
        const button = byId('upload-button');
        button.disabled = true;
        try {
          const response = await fetch('/update', {
            headers: { Authorization: authorization }, cache: 'no-store', credentials: 'omit'
          });
          if (response.status === 401) throw new Error('Update credentials are incorrect.');
          if (!response.ok) throw new Error(await responseMessage(response));
          const progress = byId('upload-progress');
          progress.hidden = false;
          progress.value = 0;
          formMessage('upload-message', 'Uploading firmware…', 'accent');
          const message = await uploadFirmware(file, authorization);
          progress.value = 100;
          formMessage('upload-message', message);
          startRestartWait(message);
        } finally {
          button.disabled = false;
        }
      }, 'upload-message');
    });

    window.addEventListener('hashchange', showPage);
    refillThermalForm();
    refillConnectionForm();
    if (view.message) formMessage('connection-message', view.message, 'muted');
    renderConfiguration();
    renderStatus();
    showPage();
    const updateClock = () => setText('clock', clockFormat.format(Date.now()));
    updateClock();
    setInterval(updateClock, 1000);
    poll();
    setInterval(poll, refreshInterval);
  </script>
  <script>
    'use strict';
    const temperatureSeries = [
      { key: 'chamber', label: 'Chamber', color: '#70d7ca' },
      { key: 'target', label: 'Target', color: '#f4c382', dashed: true },
      { key: 'cavity', label: 'U1 cavity', color: '#bba9ed' },
      { key: 'bed', label: 'Bed', color: '#ec9d86' }
    ];
    const activitySeries = [
      { key: 'heater', label: 'Heater', color: '#f4c382' },
      { key: 'fan', label: 'Fan', color: '#70d7ca' }
    ];
    const plots = [
      { canvas: byId('temp-chart'), tip: byId('temp-tip'), legend: byId('temp-legend'), series: temperatureSeries, binary: false },
      { canvas: byId('io-chart'), tip: byId('io-tip'), legend: byId('io-legend'), series: activitySeries, binary: true }
    ];
    for (const plot of plots) connectPlot(plot);
    document.addEventListener('pointerdown', event => {
      for (const plot of plots) {
        if (event.target === plot.canvas || plot.cursor === null) continue;
        plot.cursor = null;
        drawPlot(plot);
      }
    });
    byId('history-range').addEventListener('change', () => {
      for (const plot of plots) plot.cursor = null;
      redrawCharts();
    });

    function visibleHistory() {
      return samples.slice(-12 * Number(byId('history-range').value));
    }

    function connectPlot(plot) {
      plot.cursor = null;
      plot.hiddenSeries = new Set();
      for (const series of plot.series) {
        const button = document.createElement('button');
        button.type = 'button';
        button.setAttribute('aria-pressed', 'true');
        const dot = document.createElement('span');
        dot.className = 'legend-dot';
        dot.setAttribute('aria-hidden', 'true');
        dot.style.setProperty('--series-color', series.color);
        button.append(dot, document.createTextNode(series.label));
        button.addEventListener('click', () => {
          if (plot.hiddenSeries.has(series.key)) plot.hiddenSeries.delete(series.key);
          else plot.hiddenSeries.add(series.key);
          button.setAttribute('aria-pressed', String(!plot.hiddenSeries.has(series.key)));
          drawPlot(plot);
        });
        plot.legend.append(button);
      }

      function setPointer(event) {
        if (!plot.bounds) return;
        const offset = event.clientX - plot.canvas.getBoundingClientRect().left;
        plot.cursor = clamp((offset - plot.bounds.left) / plot.bounds.width, 0, 1);
        drawPlot(plot);
      }
      plot.canvas.addEventListener('pointermove', setPointer);
      plot.canvas.addEventListener('pointerdown', setPointer);
      plot.canvas.addEventListener('pointerleave', event => {
        if (event.pointerType !== 'touch') { plot.cursor = null; drawPlot(plot); }
      });
      for (const event of ['pointercancel', 'blur']) {
        plot.canvas.addEventListener(event, () => { plot.cursor = null; drawPlot(plot); });
      }
      plot.canvas.addEventListener('keydown', event => {
        if (!['ArrowLeft', 'ArrowRight', 'Home', 'End', 'Escape'].includes(event.key)) return;
        event.preventDefault();
        const step = 1 / Math.max(1, visibleHistory().length - 1);
        if (event.key === 'Escape') plot.cursor = null;
        else if (event.key === 'Home') plot.cursor = 0;
        else if (event.key === 'End') plot.cursor = 1;
        else plot.cursor = clamp((plot.cursor ?? .5) + (event.key === 'ArrowLeft' ? -step : step), 0, 1);
        drawPlot(plot, true);
      });
      if (typeof ResizeObserver === 'function') {
        const observer = new ResizeObserver(() => drawPlot(plot));
        observer.observe(plot.canvas);
      } else {
        window.addEventListener('resize', () => drawPlot(plot));
      }
    }

    function drawChamberCharts() {
      if (byId('overview').hidden) return;
      for (const plot of plots) drawPlot(plot);
    }

    function plotLine(ctx, data, key, x, y, stepped = false) {
      let previous = null;
      for (const point of data) {
        if (point[key] === null) { previous = null; continue; }
        const px = x(point);
        const py = y(point[key]);
        if (previous === null) ctx.moveTo(px, py);
        else {
          if (stepped) ctx.lineTo(px, previous);
          ctx.lineTo(px, py);
        }
        previous = py;
      }
    }

    function drawPlot(plot, announce = false) {
      const canvas = plot.canvas;
      const rect = canvas.getBoundingClientRect();
      if (!rect.width || !rect.height) { plot.tip.hidden = true; return; }
      const w = rect.width, h = rect.height;
      const dpr = Math.min(3, window.devicePixelRatio || 1);
      const pixelW = Math.round(w * dpr), pixelH = Math.round(h * dpr);
      if (canvas.width !== pixelW) canvas.width = pixelW;
      if (canvas.height !== pixelH) canvas.height = pixelH;
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      ctx.setTransform(pixelW / w, 0, 0, pixelH / h, 0, 0);
      ctx.clearRect(0, 0, w, h);
      const left = 35;
      const top = plot.binary ? 8 : 19;
      const right = w - 12;
      const bottom = h - 24;
      const width = right - left;
      plot.bounds = { left, width };
      const data = visibleHistory();
      if (!data.length) {
        plot.tip.hidden = true;
        ctx.fillStyle = '#a6b9cf';
        ctx.font = '13px system-ui, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText('Waiting for history…', w / 2, h / 2);
        return;
      }
      const oldest = data[0].at, newest = data.at(-1).at;
      const formatTick = newest - oldest < 6 * 60000 ? clockFormat : axisTime;
      const x = point => left + (point.at - oldest) / Math.max(1, newest - oldest) * width;
      const gridColor = '#2c3b4d', axisColor = '#a6b9cf';
      const timeTicks = w < 430 ? 3 : w < 680 ? 4 : 6;
      ctx.font = '11px system-ui, sans-serif';
      ctx.lineWidth = 1;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      for (let index = 0; index <= timeTicks; index++) {
        const fraction = index / timeTicks;
        const px = left + width * fraction;
        ctx.strokeStyle = gridColor;
        ctx.beginPath();
        ctx.moveTo(px, top);
        ctx.lineTo(px, bottom);
        ctx.stroke();
        ctx.fillStyle = axisColor;
        ctx.textAlign = index === timeTicks ? 'right' : 'center';
        ctx.fillText(formatTick.format(oldest + (newest - oldest) * fraction), index === timeTicks ? w - 2 : px, bottom + 7);
      }
      const enabled = plot.series.filter(series => !plot.hiddenSeries.has(series.key));
      const yFor = new Map();
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      if (plot.binary) {
        const bandHeight = (bottom - top) / 2;
        plot.series.forEach((series, index) => {
          const on = top + 6 + index * bandHeight;
          const off = on + bandHeight * .53;
          yFor.set(series.key, value => value ? on : off);
          for (const [y, label] of [[on, 'ON'], [off, 'OFF']]) {
            ctx.beginPath();
            ctx.strokeStyle = gridColor;
            ctx.moveTo(left, y);
            ctx.lineTo(right, y);
            ctx.stroke();
            ctx.fillStyle = plot.hiddenSeries.has(series.key) ? '#65778c' : series.color;
            ctx.fillText(label, left - 6, y);
          }
        });
      } else {
        const peak = Math.max(60, ...data.map(point => Math.max(point.chamber, point.target, point.cavity ?? 0, point.bed ?? 0)));
        const ceiling = Math.max(80, Math.ceil((peak + 5) / 20) * 20);
        const y = value => bottom - value / ceiling * (bottom - top);
        for (const series of plot.series) yFor.set(series.key, y);
        ctx.fillStyle = axisColor;
        ctx.fillText('°C', left - 7, 7);
        for (let temp = 0; temp <= ceiling; temp += 20) {
          const py = y(temp);
          ctx.beginPath();
          ctx.strokeStyle = gridColor;
          ctx.moveTo(left, py);
          ctx.lineTo(right, py);
          ctx.stroke();
          ctx.fillStyle = axisColor;
          ctx.fillText(String(temp), left - 7, py);
        }
        if (!plot.hiddenSeries.has('chamber')) {
          ctx.beginPath();
          plotLine(ctx, data, 'chamber', x, y);
          ctx.lineTo(right, bottom);
          ctx.lineTo(left, bottom);
          ctx.closePath();
          const fill = ctx.createLinearGradient(0, top, 0, bottom);
          fill.addColorStop(0, '#70d7ca38');
          fill.addColorStop(1, '#70d7ca07');
          ctx.fillStyle = fill;
          ctx.fill();
        }
      }

      ctx.save();
      ctx.beginPath();
      ctx.rect(left - 1, top - 1, width + 2, bottom - top + 2);
      ctx.clip();
      for (const series of enabled) {
        ctx.beginPath();
        plotLine(ctx, data, series.key, x, yFor.get(series.key), plot.binary || series.dashed);
        ctx.strokeStyle = series.color;
        ctx.lineWidth = series.key === 'chamber' ? 2.65 : 1.75;
        ctx.lineJoin = 'round';
        ctx.setLineDash(series.dashed ? [6, 4] : []);
        ctx.stroke();
      }
      ctx.restore();

      if (plot.cursor === null || enabled.length === 0) { plot.tip.hidden = true; return; }
      const sample = data[Math.round(plot.cursor * (data.length - 1))];
      const sx = x(sample);
      ctx.beginPath();
      ctx.setLineDash([3, 3]);
      ctx.lineWidth = 1;
      ctx.strokeStyle = '#a6b9cf';
      ctx.moveTo(sx, top);
      ctx.lineTo(sx, bottom);
      ctx.stroke();
      ctx.setLineDash([]);
      const title = document.createElement('strong');
      title.textContent = clockFormat.format(sample.at);
      const tooltip = [title];
      for (const series of enabled) {
        const value = sample[series.key];
        const row = document.createElement('div');
        row.className = 'tip-row';
        const name = document.createElement('span');
        name.textContent = series.label;
        const number = document.createElement('span');
        number.textContent = plot.binary ? (value ? 'ON' : 'OFF') : degrees(value);
        number.style.color = series.color;
        row.append(name, number);
        tooltip.push(row);
        if (value === null) continue;
        ctx.beginPath();
        ctx.arc(sx, yFor.get(series.key)(value), 3.2, 0, Math.PI * 2);
        ctx.fillStyle = series.color;
        ctx.fill();
      }
      plot.tip.replaceChildren(...tooltip);
      plot.tip.hidden = false;
      const tooltipWidth = plot.tip.offsetWidth;
      plot.tip.style.left = clamp(sx + (sx + tooltipWidth + 13 < w ? 9 : -tooltipWidth - 9), 0, w - tooltipWidth) + 'px';
      if (announce) setText('chart-readout', tooltip.map(row => Array.from(row.children).map(el => el.textContent).join(' ') || row.textContent).join(' · '));
    }

    window.drawChamberCharts = drawChamberCharts;
    drawChamberCharts();
  </script>
</body>
</html>
)chamberUI";
