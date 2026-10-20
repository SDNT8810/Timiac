// Shared by the development and release dashboards.
export function localDate(date = new Date()) {
  const pad = n => String(n).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}
export function clockActionNeedsRtc(action, state) {
  return action === 'show-time' || action === 'struggle' || (['next-five', 'next-hour'].includes(action) && !(state?.clock?.armMinute >= 0));
}
export function renderClockReadouts(state, connected, element) {
  const sensor = state?.proximity;
  const digital = sensor?.type === 'photoelectric';
  const hasDistance = connected && sensor?.enabled && Number.isFinite(sensor.distanceCm) && sensor.distanceCm >= 0;
  const hasPresence = connected && sensor?.enabled && typeof sensor.detected === 'boolean';
  const near = digital ? hasPresence && sensor.detected : hasDistance && sensor.distanceCm < (sensor.thresholdCm ?? 50);
  element('sensor-distance').textContent = digital ? hasPresence ? sensor.detected ? 'Detected' : 'Clear' : '—'
    : hasDistance ? `${sensor.distanceCm.toFixed(1)} cm` : '—';
  element('sensor-distance').classList.toggle('near', Boolean(near));
  element('sensor-status').textContent = !connected ? 'Disconnected' : !sensor?.enabled ? 'Disabled'
    : digital ? !hasPresence ? 'Waiting for input' : near ? 'Withdraw hand' : sensor.armed ? 'Armed' : sensor.ready ? 'Clearing…' : 'Gesture paused'
    : !hasDistance ? 'No valid echo' : near ? 'Near' : 'Clear';
  if (connected && sensor?.enabled && sensor.inputMode === 'analog') {
    const voltage = Number.isFinite(sensor.millivolts) ? `~${sensor.millivolts} mV`
      : sensor.rawAdc === 0 && Number.isFinite(sensor.adcFloorMv) ? `below ${sensor.adcFloorMv} mV` : '';
    if (voltage) element('sensor-status').textContent += ` · ${voltage}`;
  }
  const time = connected && state?.clock?.valid ? state.clock.localTime : '';
  element('clock').textContent = time ? time.slice(11) : '--:--:--';
  element('date').textContent = time ? new Date(time).toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' }) : 'Clock time unavailable';
  const internet = state?.clock?.internetSync;
  const syncStatus = internet === 'synchronized' ? 'Local time · synchronized from internet' :
    internet === 'synchronizing' ? 'Local time · synchronizing from internet…' : 'Local time · saved on your Timiac';
  element('clock-status').textContent = !connected ? 'Reconnect to read the clock.' : state?.clock?.error || syncStatus;
  if (connected && state?.clock?.armMinute >= 0) {
    const minute = state.clock.armMinute;
    element('clock-status').textContent = `Arms: ${String(Math.floor(minute / 60)).padStart(2, '0')}:${String(minute % 60).padStart(2, '0')} · ${state.clock.error || 'RTC unchanged'}`;
  }
  return time;
}
