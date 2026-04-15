const rankingBody = document.getElementById("ranking-body");
const runButton = document.getElementById("run-search");
const datasetSize = document.getElementById("dataset-size");
const modeTabs = [...document.querySelectorAll(".mode-tab")];
const idInputWrap    = document.getElementById("id-input-wrap");
const targetIdInput  = document.getElementById("target-id");
const rangeInputWrap = document.getElementById("range-input-wrap");
const rangeLoInput   = document.getElementById("range-lo");
const rangeHiInput   = document.getElementById("range-hi");
const paginationEls = {
  wrap: document.getElementById("table-pagination"),
  prev: document.getElementById("page-prev"),
  next: document.getElementById("page-next"),
  info: document.getElementById("page-info"),
};

const summaryEls = {
  count: document.getElementById("player-count"),
  height: document.getElementById("tree-height"),
  lastSearch: document.getElementById("last-search"),
  tableSubtitle: document.getElementById("table-subtitle"),
};

const resultEls = {
  linearTime: document.getElementById("linear-time"),
  linearOps: document.getElementById("linear-ops"),
  linearCaption: document.getElementById("linear-caption"),
  linearBar: document.getElementById("linear-bar"),
  btreeTime: document.getElementById("btree-time"),
  btreeOps: document.getElementById("btree-ops"),
  btreeCaption: document.getElementById("btree-caption"),
  btreeBar: document.getElementById("btree-bar"),
  bptreeTime: document.getElementById("bptree-time"),
  bptreeOps: document.getElementById("bptree-ops"),
  bptreeCaption: document.getElementById("bptree-caption"),
  bptreeBar: document.getElementById("bptree-bar"),
};

const apiBase = new URL("./", window.location.href);

let currentMode = "single";
let benchmarkData = null;
let currentRangeState = null;

const RANGE_PAGE_SIZE = 10;

function buildUrl(path) {
  return new URL(path, apiBase).toString();
}

function formatNumber(value) {
  return new Intl.NumberFormat("ko-KR").format(value);
}

function formatTimeUs(us) {
  if (typeof us !== "number") {
    return "미측정";
  }
  return `${us.toFixed(3)} μs`;
}

function formatOps(value) {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return "미측정";
  }
  return `${formatNumber(Math.round(value))}회`;
}

function formatWinRate(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return "미측정";
  }
  return `${numeric.toFixed(2)}%`;
}

function estimateTreeHeight(count) {
  if (count <= 100000) {
    return "3";
  }
  if (count <= 1000000) {
    return "4";
  }
  return "5";
}

function updateSummary(count) {
  summaryEls.count.textContent = formatNumber(Number(count));
  summaryEls.height.textContent = estimateTreeHeight(Number(count));
}

function updateIdInputVisibility() {
  idInputWrap.style.display    = currentMode === "single" ? "block" : "none";
  rangeInputWrap.style.display = currentMode === "range"  ? "flex"  : "none";
}

function getSingleSearchTarget() {
  const rawValue = targetIdInput.value.trim();
  const count = Number(datasetSize.value);

  if (!rawValue) {
    return {
      type: "id",
      value: String(Math.floor(count / 2)),
    };
  }

  if (/^\d+$/.test(rawValue) && Number(rawValue) > 0) {
    return {
      type: "id",
      value: rawValue,
    };
  }

  return {
    type: "name",
    value: rawValue,
  };
}

function getRangeLo() {
  const value = Number(rangeLoInput.value);
  const count = Number(datasetSize.value);
  if (!Number.isInteger(value) || value <= 0) {
    return 1;
  }
  return Math.min(value, count);
}

function getRangeHi() {
  const value = Number(rangeHiInput.value);
  const count = Number(datasetSize.value);
  if (!Number.isInteger(value) || value <= 0) {
    return Math.max(1, Math.floor(count * 0.01));
  }
  return Math.min(value, count);
}

function getRangeBounds() {
  const count = Number(datasetSize.value);
  let lo = getRangeLo();
  let hi = getRangeHi();

  if (hi < lo) {
    hi = lo;
  }

  lo = Math.min(lo, count);
  hi = Math.min(hi, count);

  rangeLoInput.value = lo;
  rangeHiInput.value = hi;

  return { lo, hi };
}

async function fetchJson(url, label) {
  const response = await fetch(url, { cache: "no-store" });
  const text = await response.text();

  if (!response.ok) {
    let payload = null;

    try {
      payload = JSON.parse(text);
    } catch (error) {
      payload = null;
    }

    throw new Error(
      payload && payload.message
        ? `${label}: ${payload.message}`
        : `${label}: ${response.status} ${response.statusText}`
    );
  }

  try {
    return JSON.parse(text);
  } catch (error) {
    throw new Error(`${label}: ${text}`);
  }
}

async function ensureDataset(count) {
  if (benchmarkData && benchmarkData.meta && Number(benchmarkData.meta.dataset_size) === Number(count)) {
    return benchmarkData;
  }

  runButton.textContent = "CSV 로드 및 벤치마크 실행 중...";

  const generated = await fetchJson(buildUrl(`api/generate?count=${count}`), "CSV 로드 실패");
  if (!generated.ok) {
    throw new Error(generated.message || "CSV 로드 실패");
  }

  benchmarkData = await fetchJson(
    buildUrl(`assets/results.json?ts=${Date.now()}`),
    "results.json 로드 실패"
  );
  return benchmarkData;
}

function setProgressBars(values) {
  const maxValue = Math.max(values.linear, values.btree, values.bptree, 0.001);
  resultEls.linearBar.style.width = `${Math.max(10, Math.round((values.linear / maxValue) * 100))}%`;
  resultEls.btreeBar.style.width = `${Math.max(10, Math.round((values.btree / maxValue) * 100))}%`;
  resultEls.bptreeBar.style.width = `${Math.max(10, Math.round((values.bptree / maxValue) * 100))}%`;
}

function renderRows(rows) {
  rankingBody.innerHTML = rows.map((player, index) => `
    <tr>
      <td>${player.rowNumber || index + 1}</td>
      <td>#${formatNumber(player.id)}</td>
      <td>
        <div class="player-name">
          <span>${player.nickname || player.name}</span>
          <span class="name-bar" style="width:${player.width || 96}px"></span>
        </div>
      </td>
      <td>${formatWinRate(player.win_rate)}</td>
      <td><span class="tier-badge">${player.rank}</span></td>
    </tr>
  `).join("");
}

function hidePagination() {
  paginationEls.wrap.hidden = true;
  paginationEls.prev.disabled = true;
  paginationEls.next.disabled = true;
  paginationEls.info.textContent = "";
}

function showRangePagination(data) {
  paginationEls.wrap.hidden = false;
  paginationEls.prev.disabled = !data.pageCount || data.page <= 1;
  paginationEls.next.disabled = !data.pageCount || data.page >= data.pageCount;

  if (!data.totalItems) {
    paginationEls.info.textContent = "범위 탐색 결과 없음";
    return;
  }

  paginationEls.info.textContent = `${formatNumber(data.page)} / ${formatNumber(data.pageCount)} 페이지 · 총 ${formatNumber(data.totalItems)}건`;
}

function setPaginationLoading() {
  paginationEls.wrap.hidden = false;
  paginationEls.prev.disabled = true;
  paginationEls.next.disabled = true;
  paginationEls.info.textContent = "페이지 로드 중...";
}

function renderTopPlayers(players) {
  const rows = (players || []).map((player, index) => ({
    ...player,
    rowNumber: index + 1,
    width: 76 + ((10 - index) * 6),
  }));

  renderRows(rows);
  summaryEls.tableSubtitle.textContent = "상위 10명 샘플";
  summaryEls.lastSearch.textContent = "TOP 10 Ranking";
  hidePagination();
}

function renderRange(benchmark) {
  const range = benchmark.range_search;
  const sampleRows = (benchmark.top_players || []).map((player, index) => ({
    ...player,
    rowNumber: index + 1,
    width: 76 + ((10 - index) * 6),
  }));

  resultEls.linearTime.textContent = formatTimeUs(range.linear.avg_us);
  resultEls.btreeTime.textContent = formatTimeUs(range.btree.avg_us);
  resultEls.bptreeTime.textContent = formatTimeUs(range.bptree.avg_us);

  resultEls.linearOps.textContent = formatOps(range.linear.ops);
  resultEls.btreeOps.textContent = formatOps(range.btree.ops);
  resultEls.bptreeOps.textContent = formatOps(range.bptree.ops);

  resultEls.linearCaption.textContent = `${formatNumber(range.lo)}~${formatNumber(range.hi)} 범위를 선형 탐색으로 확인`;
  resultEls.btreeCaption.textContent = `${formatNumber(range.lo)}~${formatNumber(range.hi)} 범위를 B 트리로 분기`;
  resultEls.bptreeCaption.textContent = `${formatNumber(range.lo)}~${formatNumber(range.hi)} 범위를 연결된 리프로 순회`;

  setProgressBars({
    linear: range.linear.avg_us,
    btree: range.btree.avg_us,
    bptree: range.bptree.avg_us,
  });

  summaryEls.lastSearch.textContent = `ID #${formatNumber(range.lo)} ~ ${formatNumber(range.hi)}`;
  summaryEls.tableSubtitle.textContent = "범위 탐색 실시간 결과를 불러오지 못해 상위 10명 샘플을 표시 중";
  renderRows(sampleRows);
  hidePagination();
}

// 실시간 범위 탐색 결과 렌더링 (/api/range 응답 전용)
function renderRangeRealtime(data) {
  resultEls.linearTime.textContent  = formatTimeUs(data.linear_time);
  resultEls.btreeTime.textContent   = formatTimeUs(data.btree_time);
  resultEls.bptreeTime.textContent  = formatTimeUs(data.bptree_time);

  resultEls.linearOps.textContent  = formatOps(data.linear_ops);
  resultEls.btreeOps.textContent   = formatOps(data.btree_ops);
  resultEls.bptreeOps.textContent  = formatOps(data.bptree_ops);

  const lo = formatNumber(data.lo);
  const hi = formatNumber(data.hi);
  resultEls.linearCaption.textContent  = `ID #${lo}~${hi} 범위를 선형 탐색으로 전체 확인`;
  resultEls.btreeCaption.textContent   = `ID #${lo}~${hi} 범위를 B 트리로 분기 탐색`;
  resultEls.bptreeCaption.textContent  = `ID #${lo}~${hi} 범위를 연결된 리프로 순회`;

  setProgressBars({
    linear: data.linear_time,
    btree:  data.btree_time,
    bptree: data.bptree_time,
  });

  if ((data.players || []).length > 0) {
    const startIndex = ((data.page - 1) * data.page_size) + 1;
    const rows = data.players.map((player, index) => ({
      ...player,
      rowNumber: startIndex + index,
      width: 92,
    }));
    const endIndex = startIndex + rows.length - 1;

    renderRows(rows);
    summaryEls.tableSubtitle.textContent = `범위 탐색 결과 ${formatNumber(startIndex)}-${formatNumber(endIndex)} / ${formatNumber(data.range_count)}건`;
  } else {
    rankingBody.innerHTML = `
      <tr>
        <td colspan="5">ID #${lo} ~ ${hi} 범위에 해당하는 플레이어가 없습니다.</td>
      </tr>
    `;
    summaryEls.tableSubtitle.textContent = "범위 탐색 결과 없음";
  }

  summaryEls.lastSearch.textContent = `ID #${lo} ~ ${hi}`;
  showRangePagination({
    page: data.page,
    pageCount: data.page_count,
    totalItems: data.range_count,
  });
}

function renderSingle(payload, benchmark) {
  const isNameSearch = payload.search_type === "name";
  const targetId = payload.target_id;
  const targetName = payload.target_name;
  const resolvedId = payload.resolved_id;

  resultEls.linearTime.textContent = formatTimeUs(payload.timings.linear_us);
  resultEls.btreeTime.textContent = formatTimeUs(payload.timings.btree_us);
  resultEls.bptreeTime.textContent = formatTimeUs(payload.timings.bptree_us);

  resultEls.linearOps.textContent = formatOps(payload.linear_ops);
  resultEls.btreeOps.textContent = formatOps(payload.btree_ops);
  resultEls.bptreeOps.textContent = formatOps(payload.bptree_ops);

  if (isNameSearch) {
    resultEls.linearCaption.textContent = `"${targetName}" 닉네임을 선형 탐색으로 확인`;
    if (payload.found && resolvedId > 0) {
      resultEls.btreeCaption.textContent = `찾은 ID #${formatNumber(resolvedId)}를 B 트리로 다시 조회`;
      resultEls.bptreeCaption.textContent = `찾은 ID #${formatNumber(resolvedId)}를 B+ 트리 리프에서 다시 조회`;
    } else {
      resultEls.btreeCaption.textContent = "일치하는 닉네임이 없어 B 트리 조회를 생략";
      resultEls.bptreeCaption.textContent = "일치하는 닉네임이 없어 B+ 트리 조회를 생략";
    }
  } else {
    resultEls.linearCaption.textContent = `ID #${formatNumber(targetId)}를 찾기 위해 선형 탐색 수행`;
    resultEls.btreeCaption.textContent = `ID #${formatNumber(targetId)}를 B 트리 분기로 탐색`;
    resultEls.bptreeCaption.textContent = `ID #${formatNumber(targetId)}를 B+ 트리 리프에서 탐색`;
  }

  setProgressBars({
    linear: payload.timings.linear_us,
    btree: payload.timings.btree_us,
    bptree: payload.timings.bptree_us,
  });

  summaryEls.lastSearch.textContent = isNameSearch
    ? `NAME "${targetName}"`
    : `ID #${formatNumber(targetId)}`;

  if (payload.player) {
    summaryEls.tableSubtitle.textContent = "검색 결과 1건";
    renderRows([{
      ...payload.player,
      width: 120,
    }]);
  } else {
    summaryEls.tableSubtitle.textContent = "검색 결과 없음";
    rankingBody.innerHTML = `
      <tr>
        <td colspan="5">${
          isNameSearch
            ? `닉네임 "${targetName}" 에 해당하는 플레이어가 없습니다.`
            : `ID #${formatNumber(targetId)} 에 해당하는 플레이어가 없습니다.`
        }</td>
      </tr>
    `;
  }

  hidePagination();
}

function renderTop10Realtime(data) {
  resultEls.linearTime.textContent = formatTimeUs(data.linear_time);
  resultEls.btreeTime.textContent = formatTimeUs(data.btree_time);
  resultEls.bptreeTime.textContent = formatTimeUs(data.bptree_time);

  resultEls.linearOps.textContent = formatOps(data.linear_ops);
  resultEls.btreeOps.textContent = formatOps(data.btree_ops);
  resultEls.bptreeOps.textContent = formatOps(data.bptree_ops);

  resultEls.linearCaption.textContent = "전체 배열을 순회해 승률 기준 TOP 10 집계";
  resultEls.btreeCaption.textContent = "B 트리 리프를 모두 순회해 승률 기준 TOP 10 집계";
  resultEls.bptreeCaption.textContent = "연결된 리프를 순회해 승률 기준 TOP 10 집계";

  setProgressBars({
    linear: data.linear_time,
    btree: data.btree_time,
    bptree: data.bptree_time,
  });

  renderTopPlayers(data.top_players || []);
  summaryEls.lastSearch.textContent = "TOP 10 Ranking";
  summaryEls.tableSubtitle.textContent = `실시간 TOP 10 랭킹 (${formatNumber(data.top_count || 0)}명)`;
  hidePagination();
}

async function loadRangePage(page, benchmark = null) {
  if (!currentRangeState) {
    return null;
  }

  setPaginationLoading();

  const rangePayload = await fetchJson(
    buildUrl(`api/range?lo=${currentRangeState.lo}&hi=${currentRangeState.hi}&count=${currentRangeState.count}&page=${page}&page_size=${currentRangeState.pageSize}`),
    "범위 탐색 실패"
  );

  if (!rangePayload.ok) {
    if (benchmark) {
      currentRangeState = null;
      renderRange(benchmark);
      return null;
    }

    throw new Error(rangePayload.message || "범위 탐색 실패");
  }

  currentRangeState.page = rangePayload.page;
  renderRangeRealtime(rangePayload);
  return rangePayload;
}

async function runCurrentMode() {
  const count = Number(datasetSize.value);
  updateSummary(count);

  try {
    const benchmark = await ensureDataset(count);

    if (currentMode === "single") {
      currentRangeState = null;
      const target = getSingleSearchTarget();
      runButton.textContent = "탐색 실행 중...";
      const payload = await fetchJson(
        buildUrl(`api/search?target=${encodeURIComponent(target.value)}`),
        "검색 실패"
      );
      if (!payload.ok) {
        throw new Error(payload.message || "검색 실패");
      }
      renderSingle(payload, benchmark);
    } else if (currentMode === "range") {
      const { lo, hi } = getRangeBounds();
      runButton.textContent = "탐색 실행 중...";
      currentRangeState = {
        lo,
        hi,
        count,
        page: 1,
        pageSize: RANGE_PAGE_SIZE,
      };
      await loadRangePage(1, benchmark);
    } else {
      currentRangeState = null;
      runButton.textContent = "TOP 10 집계 중...";
      const topPayload = await fetchJson(
        buildUrl(`api/top?count=${count}`),
        "TOP 10 랭킹 집계 실패"
      );
      if (!topPayload.ok) {
        throw new Error(topPayload.message || "TOP 10 랭킹 집계 실패");
      }
      renderTop10Realtime(topPayload);
    }
  } catch (error) {
    window.alert(error.message || "실행 중 오류가 발생했습니다.");
  } finally {
    runButton.textContent = "탐색 실행";
  }
}

modeTabs.forEach((button) => {
  button.addEventListener("click", () => {
    currentMode = button.dataset.mode;
    modeTabs.forEach((tab) => tab.classList.toggle("active", tab === button));
    updateIdInputVisibility();

    if (currentMode !== "range") {
      currentRangeState = null;
      hidePagination();
    }
  });
});

datasetSize.addEventListener("change", () => {
  updateSummary(datasetSize.value);
  currentRangeState = null;
  hidePagination();
});

runButton.addEventListener("click", runCurrentMode);
targetIdInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && currentMode === "single") {
    runCurrentMode();
  }
});

rangeLoInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && currentMode === "range") {
    runCurrentMode();
  }
});

rangeHiInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && currentMode === "range") {
    runCurrentMode();
  }
});

paginationEls.prev.addEventListener("click", async () => {
  if (!currentRangeState || currentRangeState.page <= 1) {
    return;
  }

  try {
    await loadRangePage(currentRangeState.page - 1);
  } catch (error) {
    window.alert(error.message || "페이지 이동 중 오류가 발생했습니다.");
  }
});

paginationEls.next.addEventListener("click", async () => {
  if (!currentRangeState) {
    return;
  }

  try {
    await loadRangePage(currentRangeState.page + 1);
  } catch (error) {
    window.alert(error.message || "페이지 이동 중 오류가 발생했습니다.");
  }
});

updateIdInputVisibility();
updateSummary(datasetSize.value);
hidePagination();
renderRows([
  { id: 91823, nickname: "ShadowBlade", win_rate: 94.72, rank: "챌린저", width: 120 },
  { id: 445221, nickname: "NightFury", win_rate: 92.48, rank: "챌린저", width: 110 },
  { id: 103942, nickname: "CrimsonAce", win_rate: 91.15, rank: "챌린저", width: 100 },
]);
