import './portal.css';

import byte90Logo from './assets/byte90_logo.svg?raw';
import wifiIcon from './assets/wifi.svg?raw';
import showIcon from './assets/show.svg?raw';
import hideIcon from './assets/hide.svg?raw';
import wifiSecureIcon from './assets/wifi_secure.svg?raw';
import wifi1BarIcon from './assets/wifi_1bar.svg?raw';
import wifi2BarIcon from './assets/wifi_2bar.svg?raw';
import wifi3BarIcon from './assets/wifi_3bar.svg?raw';
import wifi4BarIcon from './assets/wifi_4bar.svg?raw';
import wifiFindIcon from './assets/wifi_find.svg?raw';
import checkIcon from './assets/check.svg?raw';
import clockIcon from './assets/clock.svg?raw';
import effectsIcon from './assets/effects.svg?raw';
import volumeIcon from './assets/volume.svg?raw';
import expandIcon from './assets/expand.svg?raw';

interface Network {
  ssid: string;
  rssi: number;
  signal_strength: string;
  encryption_type: number;
  is_open: boolean;
  security: string;
}

interface PortalResponse {
  success: boolean;
  message?: string;
  ssid?: string;
  rssi?: number;
  connected?: boolean;
  networks?: Network[];
}

interface TimezoneLocationResponse {
  success: boolean;
  message?: string;
  timezone_name?: string;
  location?: string;
}

interface TimezoneListResponse {
  success: boolean;
  message?: string;
  timezones?: Array<{ name: string; description?: string }>;
}

interface EffectsStatusResponse {
  success: boolean;
  message?: string;
  effect?: string;
  tint?: string;
}

interface ClockStatusResponse {
  success: boolean;
  message?: string;
  enabled?: boolean;
}

interface AudioStatusResponse {
  success: boolean;
  message?: string;
  enabled?: boolean;
  volume?: number;
}

type StatusType = 'info' | 'success' | 'warning' | 'error' | 'danger';

const byte90LogoEl = document.getElementById('byte90Logo') as HTMLDivElement;
const wifiStatusCard = document.getElementById('wifiStatusCard') as HTMLDivElement;
const wifiStatusIcon = document.getElementById('wifiStatusIcon') as HTMLSpanElement;
const wifiStatusNetwork = document.getElementById('wifiStatusNetwork') as HTMLSpanElement;
const disconnectBtn = document.getElementById('disconnectBtn') as HTMLButtonElement;
const wifiCardIcon = document.getElementById('wifiCardIcon') as HTMLSpanElement;
const connectionNotification = document.getElementById('connectionNotification') as HTMLDivElement;
const networkList = document.getElementById('networkList') as HTMLUListElement;
const passwordInput = document.getElementById('password') as HTMLInputElement;
const togglePasswordBtn = document.getElementById('togglePasswordBtn') as HTMLButtonElement;
const scanBtn = document.getElementById('scanBtn') as HTMLButtonElement;
const connectBtn = document.getElementById('connectBtn') as HTMLButtonElement;
const timezoneLocationCardIcon = document.getElementById(
  'timezoneLocationCardIcon'
) as HTMLSpanElement;
const timezoneLocationNotification = document.getElementById(
  'timezoneLocationNotification'
) as HTMLDivElement;
const timezoneSelect = document.getElementById('timezoneSelect') as HTMLSelectElement;
const selectIcons = document.querySelectorAll('.select-icon') as NodeListOf<HTMLElement>;
const locationInput = document.getElementById('locationInput') as HTMLInputElement;
const timezoneLocationClearBtn = document.getElementById(
  'timezoneLocationClearBtn'
) as HTMLButtonElement;
const timezoneLocationSaveBtn = document.getElementById(
  'timezoneLocationSaveBtn'
) as HTMLButtonElement;
const clockModeToggle = document.getElementById('clockModeToggle') as HTMLButtonElement;
const audioSettingsCardIcon = document.getElementById(
  'audioSettingsCardIcon'
) as HTMLSpanElement;
const audioSettingsNotification = document.getElementById(
  'audioSettingsNotification'
) as HTMLDivElement;
const audioDisableToggle = document.getElementById(
  'audioDisableToggle'
) as HTMLButtonElement;
const audioVolumeRange = document.getElementById(
  'audioVolumeRange'
) as HTMLInputElement;
const audioVolumeValue = document.getElementById(
  'audioVolumeValue'
) as HTMLSpanElement;
const audioApplyBtn = document.getElementById('audioApplyBtn') as HTMLButtonElement;
const audioResetBtn = document.getElementById('audioResetBtn') as HTMLButtonElement;
const customizeCardIcon = document.getElementById('customizeCardIcon') as HTMLSpanElement;
const customizeNotification = document.getElementById(
  'customizeNotification'
) as HTMLDivElement;
const effectSelect = document.getElementById('effectSelect') as HTMLSelectElement;
const tintSelect = document.getElementById('tintSelect') as HTMLSelectElement;
const customizeApplyBtn = document.getElementById('customizeApplyBtn') as HTMLButtonElement;
const customizeClearBtn = document.getElementById('customizeClearBtn') as HTMLButtonElement;

const API_HEADERS = {
  'Content-Type': 'application/json',
  Accept: 'application/json',
};

const STATUS_POLL_INTERVAL_MS = 2000;
const STATUS_POLL_ATTEMPTS = 8;

let networks: Network[] = [];
let selectedNetwork = '';
let isCurrentlyConnected = false;
let connectedNetwork = '';
let isScanning = false;
let isCheckingStatus = false;
let isConnecting = false;
let isPasswordVisible = false;
let focusedIndex = -1;
let isTimezoneBusy = false;
let isLocationBusy = false;
let isClockBusy = false;
let isCustomizeBusy = false;
let isAudioBusy = false;

function svgWithClass(svg: string, className: string): string {
  if (svg.includes('class=')) {
    return svg.replace('<svg', `<svg class="${className}"`);
  }
  return svg.replace('<svg', `<svg class="${className}"`);
}

function setIcon(target: HTMLElement, svg: string, className: string) {
  target.innerHTML = svgWithClass(svg, className);
}

function initSwitch(toggle: HTMLButtonElement) {
  if (!toggle) {
    return;
  }
  if (!toggle.querySelector('.switch__toggle')) {
    const knob = document.createElement('span');
    knob.className = 'switch__toggle';
    toggle.appendChild(knob);
  }
  const color = toggle.dataset.color;
  if (color) {
    toggle.style.color = color;
  }
  updateSwitchVisual(toggle);
}

function updateSwitchVisual(toggle: HTMLButtonElement) {
  const checked = toggle.getAttribute('aria-checked') === 'true';
  const knob = toggle.querySelector('.switch__toggle') as HTMLElement | null;
  if (!knob) {
    return;
  }
  if (checked) {
    const color = toggle.dataset.color;
    if (color) {
      toggle.style.backgroundColor = color;
    }
    knob.style.transform = 'translateX(28px)';
  } else {
    toggle.style.backgroundColor = '';
    knob.style.transform = 'translateX(0)';
  }
}

function setSwitchChecked(toggle: HTMLButtonElement, checked: boolean) {
  toggle.setAttribute('aria-checked', checked ? 'true' : 'false');
  updateSwitchVisual(toggle);
}

function isSwitchChecked(toggle: HTMLButtonElement): boolean {
  return toggle.getAttribute('aria-checked') === 'true';
}

function updateRangeValue(input: HTMLInputElement, output: HTMLElement) {
  output.textContent = input.value;
}

function setNotification(message: string, type: StatusType = 'info') {
  if (!message) {
    connectionNotification.hidden = true;
    connectionNotification.textContent = '';
    return;
  }

  connectionNotification.hidden = false;
  connectionNotification.textContent = message;

  if (type === 'error' || type === 'danger') {
    connectionNotification.setAttribute('role', 'alert');
    connectionNotification.setAttribute('aria-live', 'assertive');
  } else if (type === 'warning') {
    connectionNotification.setAttribute('role', 'alert');
    connectionNotification.setAttribute('aria-live', 'polite');
  } else {
    connectionNotification.setAttribute('role', 'status');
    connectionNotification.setAttribute('aria-live', 'polite');
  }
}

function setTimezoneLocationNotification(message: string, type: StatusType = 'info') {
  if (!message) {
    timezoneLocationNotification.hidden = true;
    timezoneLocationNotification.textContent = '';
    return;
  }

  timezoneLocationNotification.hidden = false;
  timezoneLocationNotification.textContent = message;

  if (type === 'error' || type === 'danger') {
    timezoneLocationNotification.setAttribute('role', 'alert');
    timezoneLocationNotification.setAttribute('aria-live', 'assertive');
  } else if (type === 'warning') {
    timezoneLocationNotification.setAttribute('role', 'alert');
    timezoneLocationNotification.setAttribute('aria-live', 'polite');
  } else {
    timezoneLocationNotification.setAttribute('role', 'status');
    timezoneLocationNotification.setAttribute('aria-live', 'polite');
  }
}

function setCustomizeNotification(message: string, type: StatusType = 'info') {
  if (!message) {
    customizeNotification.hidden = true;
    customizeNotification.textContent = '';
    return;
  }

  customizeNotification.hidden = false;
  customizeNotification.textContent = message;

  if (type === 'error' || type === 'danger') {
    customizeNotification.setAttribute('role', 'alert');
    customizeNotification.setAttribute('aria-live', 'assertive');
  } else if (type === 'warning') {
    customizeNotification.setAttribute('role', 'alert');
    customizeNotification.setAttribute('aria-live', 'polite');
  } else {
    customizeNotification.setAttribute('role', 'status');
    customizeNotification.setAttribute('aria-live', 'polite');
  }
}

function setAudioSettingsNotification(message: string, type: StatusType = 'info') {
  if (!message) {
    audioSettingsNotification.hidden = true;
    audioSettingsNotification.textContent = '';
    return;
  }

  audioSettingsNotification.hidden = false;
  audioSettingsNotification.textContent = message;

  if (type === 'error' || type === 'danger') {
    audioSettingsNotification.setAttribute('role', 'alert');
    audioSettingsNotification.setAttribute('aria-live', 'assertive');
  } else if (type === 'warning') {
    audioSettingsNotification.setAttribute('role', 'alert');
    audioSettingsNotification.setAttribute('aria-live', 'polite');
  } else {
    audioSettingsNotification.setAttribute('role', 'status');
    audioSettingsNotification.setAttribute('aria-live', 'polite');
  }
}
function updateWifiStatusCard() {
  if (!isCurrentlyConnected) {
    wifiStatusCard.hidden = true;
    return;
  }

  wifiStatusNetwork.textContent = connectedNetwork;
  wifiStatusCard.hidden = false;
}

function formatTimezoneLabel(name: string) {
  return name.replace(/_/g, ' ');
}

async function populateTimezoneOptions() {
  timezoneSelect.innerHTML = '';
  const placeholder = document.createElement('option');
  placeholder.value = '';
  placeholder.textContent = 'Select timezone';
  timezoneSelect.appendChild(placeholder);

  try {
    const data = await fetchTimezoneList();
    const timezones = Array.isArray(data.timezones) ? data.timezones : [];
    timezones
      .map(item => ({
        value: item.name,
        label: formatTimezoneLabel(item.name),
      }))
      .sort((a, b) => a.label.localeCompare(b.label))
      .forEach(option => {
        const element = document.createElement('option');
        element.value = option.value;
        element.textContent = option.label;
        timezoneSelect.appendChild(element);
      });
  } catch (error) {
    console.error('Timezone list fetch failed:', error);
    setTimezoneLocationNotification(
      error instanceof Error ? error.message : 'Failed to load timezones.',
      'error'
    );
  }
}

function getSignalIcon(rssi: number): string {
  if (rssi >= -50) return wifi4BarIcon;
  if (rssi >= -60) return wifi3BarIcon;
  if (rssi >= -70) return wifi2BarIcon;
  return wifi1BarIcon;
}

function renderEmptyNetworkState() {
  networkList.innerHTML = '';
  const emptyItem = document.createElement('li');
  emptyItem.className = 'networks__list-empty-state';
  emptyItem.innerHTML = `${svgWithClass(wifiFindIcon, 'wifi-find-icon')}<p>Scan for available networks</p>`;
  networkList.appendChild(emptyItem);
}

function renderNetworkList() {
  networkList.innerHTML = '';

  if (networks.length === 0) {
    renderEmptyNetworkState();
    return;
  }

  networks.forEach((network, index) => {
    const item = document.createElement('li');
    item.className = 'networks__item';
    item.id = `network-option-${index}`;
    item.setAttribute('role', 'option');
    item.setAttribute('tabindex', '-1');
    item.setAttribute('aria-selected', String(selectedNetwork === network.ssid));

    const ssidSpan = document.createElement('span');
    ssidSpan.className = 'networks__item-ssid';
    ssidSpan.textContent = network.ssid;

    const details = document.createElement('div');
    details.className = 'networks__item-details';

    if (connectedNetwork === network.ssid && isCurrentlyConnected) {
      const connectedSpan = document.createElement('span');
      connectedSpan.title = 'Currently connected';
      connectedSpan.innerHTML = svgWithClass(checkIcon, 'wifi-connected-icon');
      details.appendChild(connectedSpan);
    }

    const securitySpan = document.createElement('span');
    securitySpan.title = network.is_open ? 'Open' : 'Secured';
    if (!network.is_open) {
      securitySpan.innerHTML = svgWithClass(wifiSecureIcon, 'wifi-security-icon');
    }
    details.appendChild(securitySpan);

    const signalSpan = document.createElement('span');
    signalSpan.title = `Signal strength: ${network.signal_strength} (${network.rssi} dBm)`;
    signalSpan.innerHTML = svgWithClass(getSignalIcon(network.rssi), 'wifi-signal-icon');
    details.appendChild(signalSpan);

    item.appendChild(ssidSpan);
    item.appendChild(details);

    item.addEventListener('click', () => {
      focusedIndex = index;
      selectNetwork(network.ssid);
    });

    networkList.appendChild(item);
  });

  if (focusedIndex >= 0) {
    networkList.setAttribute('aria-activedescendant', `network-option-${focusedIndex}`);
  } else {
    networkList.removeAttribute('aria-activedescendant');
  }
}

function selectNetwork(ssid: string) {
  selectedNetwork = ssid;
  renderNetworkList();
  updateButtons();
}

function requiresPassword(): boolean {
  const network = networks.find(item => item.ssid === selectedNetwork);
  if (!network) {
    return true;
  }
  return !network.is_open;
}

function updateButtons() {
  scanBtn.disabled = isScanning || isConnecting || isCheckingStatus;
  const isSelectedConnected =
    isCurrentlyConnected && connectedNetwork && selectedNetwork === connectedNetwork;
  connectBtn.disabled =
    isConnecting ||
    isCheckingStatus ||
    !selectedNetwork ||
    (!isSelectedConnected &&
      requiresPassword() &&
      passwordInput.value.trim().length === 0);
  disconnectBtn.disabled = isConnecting || isCheckingStatus || !isCurrentlyConnected;
  passwordInput.disabled = isConnecting;
  togglePasswordBtn.disabled = isConnecting;
  if (isConnecting) {
    connectBtn.textContent = 'Connecting...';
  } else if (isSelectedConnected) {
    connectBtn.textContent = 'Disconnect';
  } else {
    connectBtn.textContent = 'Connect';
  }

  const timezoneBusy = isTimezoneBusy || isLocationBusy;
  timezoneSelect.disabled = timezoneBusy;
  locationInput.disabled = timezoneBusy;
  clockModeToggle.disabled = timezoneBusy || isClockBusy;
  const hasTimezoneInput = timezoneSelect.value.trim().length > 0;
  const hasLocationInput = locationInput.value.trim().length > 0;
  timezoneLocationSaveBtn.disabled = !hasTimezoneInput && !hasLocationInput;
  timezoneLocationClearBtn.disabled = timezoneBusy || (!hasTimezoneInput && !hasLocationInput);

  effectSelect.disabled = isCustomizeBusy;
  tintSelect.disabled = isCustomizeBusy;
  customizeApplyBtn.disabled = false;
  customizeClearBtn.disabled = isCustomizeBusy;
  if (isCustomizeBusy) {
    effectSelect.classList.remove('error');
    tintSelect.classList.remove('error');
  }

  audioDisableToggle.disabled = isAudioBusy;
  audioVolumeRange.disabled = isAudioBusy;
  audioApplyBtn.disabled = false;
  audioResetBtn.disabled = isAudioBusy;
}

function scrollToFocusedItem(index: number) {
  const focusedElement = document.getElementById(`network-option-${index}`);
  if (focusedElement) {
    focusedElement.scrollIntoView({ block: 'center', behavior: 'smooth' });
  }
}

function runWithButtonFocus(
  button: HTMLButtonElement,
  action: () => Promise<void>
): void {
  action().finally(() => {
    if (!button.disabled && button.offsetParent !== null) {
      setTimeout(() => {
        if (!button.disabled && button.offsetParent !== null) {
          button.focus();
        }
      }, 0);
    }
  });
}

function handleListboxKeyDown(event: KeyboardEvent) {
  if (networks.length === 0) {
    return;
  }

  switch (event.key) {
    case 'Tab':
      break;
    case 'ArrowDown': {
      event.preventDefault();
      const nextIndex = focusedIndex < networks.length - 1 ? focusedIndex + 1 : 0;
      focusedIndex = nextIndex;
      selectNetwork(networks[nextIndex].ssid);
      scrollToFocusedItem(nextIndex);
      break;
    }
    case 'ArrowUp': {
      event.preventDefault();
      const prevIndex = focusedIndex > 0 ? focusedIndex - 1 : networks.length - 1;
      focusedIndex = prevIndex;
      selectNetwork(networks[prevIndex].ssid);
      scrollToFocusedItem(prevIndex);
      break;
    }
    case 'Home': {
      event.preventDefault();
      focusedIndex = 0;
      selectNetwork(networks[0].ssid);
      scrollToFocusedItem(0);
      break;
    }
    case 'End': {
      event.preventDefault();
      const lastIndex = networks.length - 1;
      focusedIndex = lastIndex;
      selectNetwork(networks[lastIndex].ssid);
      scrollToFocusedItem(lastIndex);
      break;
    }
    case 'Enter':
    case ' ': {
      event.preventDefault();
      if (focusedIndex >= 0) {
        selectNetwork(networks[focusedIndex].ssid);
      }
      break;
    }
  }
}

async function fetchPortalJson(path: string, init?: RequestInit): Promise<PortalResponse> {
  const response = await fetch(path, {
    cache: 'no-store',
    ...init,
    headers: {
      ...API_HEADERS,
      ...(init?.headers || {}),
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      `Unexpected response for ${path}. ` +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as PortalResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchTimezoneLocationJson(
  path: string,
  init?: RequestInit
): Promise<TimezoneLocationResponse> {
  const response = await fetch(path, {
    cache: 'no-store',
    ...init,
    headers: {
      ...API_HEADERS,
      ...(init?.headers || {}),
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      `Unexpected response for ${path}. ` +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as TimezoneLocationResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchClockStatusJson(
  path: string,
  init?: RequestInit
): Promise<ClockStatusResponse> {
  const response = await fetch(path, {
    cache: 'no-store',
    ...init,
    headers: {
      ...API_HEADERS,
      ...(init?.headers || {}),
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      `Unexpected response for ${path}. ` +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as ClockStatusResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchAudioStatusJson(
  path: string,
  init?: RequestInit
): Promise<AudioStatusResponse> {
  const response = await fetch(path, {
    cache: 'no-store',
    ...init,
    headers: {
      ...API_HEADERS,
      ...(init?.headers || {}),
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      `Unexpected response for ${path}. ` +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as AudioStatusResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchEffectsJson(init?: RequestInit): Promise<EffectsStatusResponse> {
  const response = await fetch('/api/effects', {
    cache: 'no-store',
    ...init,
    headers: {
      ...API_HEADERS,
      ...(init?.headers || {}),
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      'Unexpected response for /api/effects. ' +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as EffectsStatusResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchEffectsStatusJson(): Promise<EffectsStatusResponse> {
  const response = await fetch('/api/effects/status', {
    cache: 'no-store',
    headers: {
      ...API_HEADERS,
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      'Unexpected response for /api/effects/status. ' +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as EffectsStatusResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchTimezoneList(): Promise<TimezoneListResponse> {
  const response = await fetch('/api/timezone/list', {
    cache: 'no-store',
    headers: {
      ...API_HEADERS,
    },
  });

  const contentType = response.headers.get('content-type') || '';
  const bodyText = await response.text();

  if (!contentType.includes('application/json')) {
    throw new Error(
      'Unexpected response for /api/timezone/list. ' +
        'Make sure you are connected to the device AP and loading the portal from the ESP32.'
    );
  }

  const data = JSON.parse(bodyText) as TimezoneListResponse;
  if (!response.ok || !data.success) {
    throw new Error(data.message || 'Request failed.');
  }

  return data;
}

async function fetchTimezoneLocationStatus() {
  if (isTimezoneBusy || isLocationBusy) {
    return;
  }

  isTimezoneBusy = true;
  isLocationBusy = true;
  updateButtons();

  try {
    const [timezoneData, locationData] = await Promise.all([
      fetchTimezoneLocationJson('/api/timezone/status'),
      fetchTimezoneLocationJson('/api/location/status'),
    ]);

    timezoneSelect.value = timezoneData.timezone_name || '';
    locationInput.value = locationData.location || '';
  } catch (error) {
    console.error('Timezone/location status failed:', error);
    setTimezoneLocationNotification(
      error instanceof Error ? error.message : 'Timezone/location status failed.',
      'error'
    );
  } finally {
    isTimezoneBusy = false;
    isLocationBusy = false;
    updateButtons();
  }
}

async function fetchClockStatus() {
  if (isClockBusy) {
    return;
  }

  isClockBusy = true;
  updateButtons();

  try {
    const data = await fetchClockStatusJson('/api/clock/status');
  setSwitchChecked(clockModeToggle, Boolean(data.enabled));
  } catch (error) {
    console.error('Clock status failed:', error);
    setTimezoneLocationNotification(
      error instanceof Error ? error.message : 'Clock status failed.',
      'error'
    );
  } finally {
    isClockBusy = false;
    updateButtons();
  }
}

async function fetchAudioStatus() {
  if (isAudioBusy) {
    return;
  }

  isAudioBusy = true;
  updateButtons();

  try {
    const data = await fetchAudioStatusJson('/api/audio/status');
    const disabled = data.enabled === false;
    setSwitchChecked(audioDisableToggle, disabled);
    if (typeof data.volume === 'number') {
      audioVolumeRange.value = String(data.volume);
      updateRangeValue(audioVolumeRange, audioVolumeValue);
    }
  } catch (error) {
    console.error('Audio status failed:', error);
    setAudioSettingsNotification(
      error instanceof Error ? error.message : 'Audio status failed.',
      'error'
    );
  } finally {
    isAudioBusy = false;
    updateButtons();
  }
}

async function applyAudioSettings() {
  if (isAudioBusy) {
    return;
  }

  const disabled = isSwitchChecked(audioDisableToggle);
  const volume = Number.parseInt(audioVolumeRange.value, 10);

  isAudioBusy = true;
  setAudioSettingsNotification('Applying audio settings...', 'info');
  updateButtons();

  try {
    const data = await fetchAudioStatusJson('/api/audio', {
      method: 'POST',
      body: JSON.stringify({ disabled, volume }),
    });
    setSwitchChecked(audioDisableToggle, data.enabled === false);
    if (typeof data.volume === 'number') {
      audioVolumeRange.value = String(data.volume);
      updateRangeValue(audioVolumeRange, audioVolumeValue);
    }
    setAudioSettingsNotification('Audio settings updated.', 'success');
  } catch (error) {
    console.error('Audio apply failed:', error);
    setAudioSettingsNotification(
      error instanceof Error ? error.message : 'Failed to apply audio settings.',
      'error'
    );
  } finally {
    isAudioBusy = false;
    updateButtons();
  }
}

async function resetAudioSettings() {
  if (isAudioBusy) {
    return;
  }

  isAudioBusy = true;
  setAudioSettingsNotification('Resetting audio settings...', 'info');
  updateButtons();

  try {
    const data = await fetchAudioStatusJson('/api/audio/reset', {
      method: 'POST',
    });
    setSwitchChecked(audioDisableToggle, data.enabled === false);
    if (typeof data.volume === 'number') {
      audioVolumeRange.value = String(data.volume);
      updateRangeValue(audioVolumeRange, audioVolumeValue);
    }
    setAudioSettingsNotification('Audio settings reset.', 'success');
  } catch (error) {
    console.error('Audio reset failed:', error);
    setAudioSettingsNotification(
      error instanceof Error ? error.message : 'Failed to reset audio settings.',
      'error'
    );
  } finally {
    isAudioBusy = false;
    updateButtons();
  }
}

async function fetchEffectsStatus() {
  if (isCustomizeBusy) {
    return;
  }

  isCustomizeBusy = true;
  updateButtons();

  try {
    const data = await fetchEffectsStatusJson();
    if (data.effect) {
      effectSelect.value = data.effect;
    }
    if (data.tint) {
      tintSelect.value = data.tint;
    }
  } catch (error) {
    console.error('Effects status failed:', error);
    setCustomizeNotification(
      error instanceof Error ? error.message : 'Effects status failed.',
      'error'
    );
  } finally {
    isCustomizeBusy = false;
    updateButtons();
  }
}

async function applyCustomization() {
  if (isCustomizeBusy) {
    return;
  }

  const effect = effectSelect.value || 'none';
  const tint = tintSelect.value || 'none';

  isCustomizeBusy = true;
  setCustomizeNotification('Applying effects...', 'info');
  updateButtons();

  try {
    const data = await fetchEffectsJson({
      method: 'POST',
      body: JSON.stringify({ effect, tint }),
    });
    if (data.effect) {
      effectSelect.value = data.effect;
    }
    if (data.tint) {
      tintSelect.value = data.tint;
    }
    setCustomizeNotification('Effects updated.', 'success');
  } catch (error) {
    console.error('Effects update failed:', error);
    setCustomizeNotification(
      error instanceof Error ? error.message : 'Failed to update effects.',
      'error'
    );
  } finally {
    isCustomizeBusy = false;
    updateButtons();
  }
}

async function clearEffects() {
  if (isCustomizeBusy) {
    return;
  }

  isCustomizeBusy = true;
  setCustomizeNotification('Clearing effects...', 'info');
  updateButtons();

  try {
    const data = await fetchEffectsJson({
      method: 'POST',
      body: JSON.stringify({ effect: 'none', tint: 'none' }),
    });
    effectSelect.value = data.effect || 'none';
    tintSelect.value = data.tint || 'none';
    setCustomizeNotification('Effects cleared.', 'success');
  } catch (error) {
    console.error('Clear effects failed:', error);
    setCustomizeNotification(
      error instanceof Error ? error.message : 'Failed to clear effects.',
      'error'
    );
  } finally {
    isCustomizeBusy = false;
    updateButtons();
  }
}

async function saveTimezoneLocation() {
  if (isTimezoneBusy || isLocationBusy) {
    return;
  }

  const timezoneName = timezoneSelect.value.trim();
  const location = locationInput.value.trim();
  if (!timezoneName && !location) {
    setTimezoneLocationNotification('Select a timezone or enter a location.', 'warning');
    return;
  }

  isTimezoneBusy = true;
  isLocationBusy = true;
  isClockBusy = true;
  setTimezoneLocationNotification('Saving timezone/location...', 'info');
  updateButtons();

  try {
    if (timezoneName) {
      await fetchTimezoneLocationJson('/api/timezone', {
        method: 'POST',
        body: JSON.stringify({ timezone_name: timezoneName }),
      });
    }

    if (location) {
      await fetchTimezoneLocationJson('/api/location', {
        method: 'POST',
        body: JSON.stringify({ location }),
      });
    }

    if (isSwitchChecked(clockModeToggle)) {
      if (!timezoneName) {
        setTimezoneLocationNotification(
          'Timezone required to enable clock mode.',
          'warning'
        );
      } else {
        await fetchClockStatusJson('/api/clock', {
          method: 'POST',
          body: JSON.stringify({ enabled: true, timezone_name: timezoneName }),
        });
      }
    } else {
      await fetchClockStatusJson('/api/clock', {
        method: 'POST',
        body: JSON.stringify({ enabled: false }),
      });
    }

    setTimezoneLocationNotification('Timezone/location saved successfully.', 'success');
  } catch (error) {
    console.error('Timezone/location save failed:', error);
    setTimezoneLocationNotification(
      error instanceof Error ? error.message : 'Failed to save timezone/location.',
      'error'
    );
  } finally {
    isTimezoneBusy = false;
    isLocationBusy = false;
    isClockBusy = false;
    updateButtons();
  }
}

async function clearTimezoneLocation() {
  if (isTimezoneBusy || isLocationBusy) {
    return;
  }

  isTimezoneBusy = true;
  isLocationBusy = true;
  setTimezoneLocationNotification('Clearing timezone/location...', 'info');
  updateButtons();

  try {
    await fetchTimezoneLocationJson('/api/timezone/clear', { method: 'POST' });
    await fetchTimezoneLocationJson('/api/location/clear', { method: 'POST' });
    timezoneSelect.value = '';
    locationInput.value = '';
    setTimezoneLocationNotification('Timezone/location cleared successfully.', 'success');
  } catch (error) {
    console.error('Timezone/location clear failed:', error);
    setTimezoneLocationNotification(
      error instanceof Error ? error.message : 'Failed to clear timezone/location.',
      'error'
    );
  } finally {
    isTimezoneBusy = false;
    isLocationBusy = false;
    updateButtons();
  }
}

async function scanNetworks() {
  if (isScanning) {
    return;
  }

  isScanning = true;
  setNotification('Scanning for networks...', 'info');
  updateButtons();

  try {
    const data = await fetchPortalJson('/api/scan');
    networks = Array.isArray(data.networks) ? data.networks : [];
    setNotification(data.message || 'Scan complete.', 'success');
    renderNetworkList();
  } catch (error) {
    console.error('Scan failed:', error);
    setNotification(
      error instanceof Error ? error.message : 'Scan failed. Please try again.',
      'error'
    );
    networks = [];
    renderNetworkList();
  } finally {
    isScanning = false;
    updateButtons();
  }
}

async function checkWiFiStatus() {
  if (isCheckingStatus) {
    return;
  }

  isCheckingStatus = true;
  updateButtons();

  try {
    const data = await fetchPortalJson('/api/status');
    isCurrentlyConnected = data.connected === true;
    connectedNetwork = typeof data.ssid === 'string' ? data.ssid : '';
    updateWifiStatusCard();
    renderNetworkList();
    if (!isScanning) {
      setNotification(
        data.message || 'Status updated.',
        data.connected ? 'success' : 'info'
      );
    }
  } catch (error) {
    console.error('Status check failed:', error);
    setNotification(
      error instanceof Error ? error.message : 'Status check failed.',
      'error'
    );
  } finally {
    isCheckingStatus = false;
    updateButtons();
  }
}

async function connectToNetwork() {
  if (isConnecting || !selectedNetwork) {
    return;
  }

  if (requiresPassword() && passwordInput.value.trim().length === 0) {
    return;
  }

  isConnecting = true;
  setNotification(`Connecting to ${selectedNetwork}...`, 'info');
  updateButtons();

  try {
    const data = await fetchPortalJson('/api/configure', {
      method: 'POST',
      body: JSON.stringify({
        ssid: selectedNetwork,
        password: passwordInput.value,
      }),
    });

    setNotification(data.message || 'Connection request sent.', 'info');

    for (let attempt = 0; attempt < STATUS_POLL_ATTEMPTS; attempt++) {
      await new Promise<void>(resolve => {
        setTimeout(resolve, STATUS_POLL_INTERVAL_MS);
      });

      const statusData = await fetchPortalJson('/api/status');
      isCurrentlyConnected = statusData.connected === true;
      connectedNetwork = typeof statusData.ssid === 'string' ? statusData.ssid : '';
      updateWifiStatusCard();
      renderNetworkList();

      if (statusData.connected) {
        setNotification(statusData.message || 'Connected.', 'success');
        break;
      }
    }

    if (!isCurrentlyConnected) {
      setNotification('Connection in progress...', 'info');
    }
  } catch (error) {
    console.error('Connection failed:', error);
    setNotification(
      error instanceof Error ? error.message : 'Connection failed.',
      'error'
    );
  } finally {
    isConnecting = false;
    updateButtons();
  }
}

async function disconnectFromNetwork() {
  if (isConnecting || isCheckingStatus || !isCurrentlyConnected) {
    return;
  }

  isCheckingStatus = true;
  setNotification('Disconnecting...', 'info');
  updateButtons();

  try {
    const data = await fetchPortalJson('/api/disconnect', { method: 'POST' });
    isCurrentlyConnected = false;
    connectedNetwork = '';
    updateWifiStatusCard();
    renderNetworkList();
    setNotification(data.message || 'Disconnected.', 'success');
  } catch (error) {
    console.error('Disconnect failed:', error);
    setNotification(
      error instanceof Error ? error.message : 'Disconnect failed.',
      'error'
    );
  } finally {
    isCheckingStatus = false;
    updateButtons();
  }
}

function togglePasswordVisibility() {
  isPasswordVisible = !isPasswordVisible;
  passwordInput.type = isPasswordVisible ? 'text' : 'password';
  togglePasswordBtn.setAttribute(
    'aria-label',
    isPasswordVisible ? 'Hide password' : 'Show password'
  );
  togglePasswordBtn.innerHTML = svgWithClass(
    isPasswordVisible ? hideIcon : showIcon,
    'password-icon'
  );
}

function initialize() {
  setIcon(byte90LogoEl, byte90Logo, 'byte90-logo');
  setIcon(wifiStatusIcon, wifiIcon, 'wifi-icon');
  setIcon(wifiCardIcon, wifiIcon, 'card-component__icon');
  setIcon(timezoneLocationCardIcon, clockIcon, 'card-component__icon');
  setIcon(customizeCardIcon, effectsIcon, 'card-component__icon');
  setIcon(audioSettingsCardIcon, volumeIcon, 'card-component__icon');
  selectIcons.forEach(icon => setIcon(icon, expandIcon, 'select-icon'));
  togglePasswordBtn.innerHTML = svgWithClass(showIcon, 'password-icon');
  initSwitch(clockModeToggle);
  initSwitch(audioDisableToggle);
  updateRangeValue(audioVolumeRange, audioVolumeValue);

  renderNetworkList();
  updateButtons();
  updateWifiStatusCard();
  setNotification('Scanning for networks...', 'info');

  scanBtn.addEventListener('click', scanNetworks);
  connectBtn.addEventListener('click', () => {
    const isSelectedConnected =
      isCurrentlyConnected && connectedNetwork && selectedNetwork === connectedNetwork;
    if (isSelectedConnected) {
      disconnectFromNetwork();
      return;
    }
    connectToNetwork();
  });
  disconnectBtn.addEventListener('click', disconnectFromNetwork);
  togglePasswordBtn.addEventListener('click', togglePasswordVisibility);
  passwordInput.addEventListener('input', updateButtons);
  networkList.addEventListener('keydown', handleListboxKeyDown);
  timezoneLocationSaveBtn.addEventListener('click', () => {
    runWithButtonFocus(timezoneLocationSaveBtn, saveTimezoneLocation);
  });
  timezoneLocationClearBtn.addEventListener('click', () => {
    runWithButtonFocus(timezoneLocationClearBtn, clearTimezoneLocation);
  });
  timezoneSelect.addEventListener('change', updateButtons);
  locationInput.addEventListener('input', updateButtons);
  clockModeToggle.addEventListener('click', () => {
    setSwitchChecked(clockModeToggle, !isSwitchChecked(clockModeToggle));
  });
  audioDisableToggle.addEventListener('click', () => {
    setSwitchChecked(audioDisableToggle, !isSwitchChecked(audioDisableToggle));
  });
  audioVolumeRange.addEventListener('input', () => {
    updateRangeValue(audioVolumeRange, audioVolumeValue);
  });
  effectSelect.addEventListener('change', updateButtons);
  tintSelect.addEventListener('change', updateButtons);
  customizeApplyBtn.addEventListener('click', () => {
    runWithButtonFocus(customizeApplyBtn, applyCustomization);
  });
  customizeClearBtn.addEventListener('click', () => {
    runWithButtonFocus(customizeClearBtn, clearEffects);
  });
  audioApplyBtn.addEventListener('click', () => {
    runWithButtonFocus(audioApplyBtn, applyAudioSettings);
  });
  audioResetBtn.addEventListener('click', () => {
    runWithButtonFocus(audioResetBtn, resetAudioSettings);
  });

  loadInitialStatusAndScan();
}

async function loadInitialStatusAndScan() {
  await populateTimezoneOptions();
  await fetchTimezoneLocationStatus();
  await fetchClockStatus();
  await fetchEffectsStatus();
  await fetchAudioStatus();
  await checkWiFiStatus();
  await scanNetworks();
}

initialize();
