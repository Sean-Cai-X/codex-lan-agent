package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/data/binding"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

const (
	appID          = "com.native.clash"
	appTitle       = "Clash Native"
	coreFileName   = "clash-meta.exe"
	configFileName = "config.yaml"
	apiBaseURL     = "http://127.0.0.1:9090"
	testURL        = "http://www.gstatic.com/generate_204"
)

type clashProxy struct {
	Name string   `json:"name"`
	Type string   `json:"type"`
	Now  string   `json:"now"`
	All  []string `json:"all"`
}

type proxiesResponse struct {
	Proxies map[string]clashProxy `json:"proxies"`
}

type delayResponse struct {
	Delay int `json:"delay"`
}

type apiSettings struct {
	BaseURL     string
	Secret      string
	ProxyServer string
}

var state = struct {
	sync.Mutex
	cmd     *exec.Cmd
	running bool
}{}

var logState = struct {
	sync.Mutex
	lines []string
	text  binding.String
}{}

func main() {
	a := app.NewWithID(appID)
	// App name is provided by package metadata and the window title.

	w := a.NewWindow(appTitle + " - No WebView")
	w.Resize(fyne.NewSize(900, 620))

	logState.text = binding.NewString()
	_ = logState.text.Set("Ready")

	subInput := widget.NewEntry()
	subInput.SetPlaceHolder("Subscription URL")

	apiInput := widget.NewEntry()
	apiInput.SetText(apiBaseURL)

	secretInput := widget.NewPasswordEntry()
	secretInput.SetPlaceHolder("API secret, leave empty when config.yaml has no secret")

	proxyInput := widget.NewEntry()
	proxyInput.SetText("127.0.0.1:7890")

	currentSettings := func() apiSettings {
		baseURL := strings.TrimRight(strings.TrimSpace(apiInput.Text), "/")
		if baseURL == "" {
			baseURL = apiBaseURL
		}
		proxyServer := strings.TrimSpace(proxyInput.Text)
		if proxyServer == "" {
			proxyServer = "127.0.0.1:7890"
		}
		return apiSettings{
			BaseURL:     baseURL,
			Secret:      strings.TrimSpace(secretInput.Text),
			ProxyServer: proxyServer,
		}
	}

	statusLabel := widget.NewLabel("Core: stopped")
	nodeSelect := widget.NewSelect([]string{}, nil)
	nodeSelect.PlaceHolder = "Refresh nodes first"

	logText := widget.NewMultiLineEntry()
	logText.Bind(logState.text)
	logText.Disable()
	logText.SetMinRowsVisible(15)

	refreshNodes := func() {
		go func() {
			names, now, err := loadNodeNames(currentSettings())
			if err != nil {
				addLog("refresh nodes failed: " + err.Error())
				return
			}
			if len(names) == 0 {
				addLog("no selectable nodes found from Clash API")
				return
			}
			nodeSelect.Options = names
			if now != "" {
				nodeSelect.SetSelected(now)
			}
			nodeSelect.Refresh()
			addLog(fmt.Sprintf("loaded %d nodes", len(names)))
		}()
	}

	nodeSelect.OnChanged = func(name string) {
		if name == "" {
			return
		}
		go func() {
			if err := selectGlobalNode(currentSettings(), name); err != nil {
				addLog("switch node failed: " + err.Error())
				return
			}
			addLog("selected node: " + name)
		}()
	}

	addSubBtn := widget.NewButtonWithIcon("Add", theme.ContentAddIcon(), func() {
		if strings.TrimSpace(subInput.Text) == "" {
			dialog.ShowError(errors.New("subscription URL is empty"), w)
			return
		}
		go func() {
			if err := fetchSubscription(subInput.Text); err != nil {
				addLog("subscription failed: " + err.Error())
				return
			}
			addLog("subscription saved to " + configFileName)
		}()
	})

	startBtn := widget.NewButtonWithIcon("Start", theme.MediaPlayIcon(), func() {
		go func() {
			if err := startClash(); err != nil {
				addLog("start core failed: " + err.Error())
				dialog.ShowError(err, w)
				return
			}
			statusLabel.SetText("Core: running")
		}()
	})

	stopBtn := widget.NewButtonWithIcon("Stop", theme.MediaStopIcon(), func() {
		if stopped := stopClash(); stopped {
			statusLabel.SetText("Core: stopped")
		}
	})

	refreshBtn := widget.NewButtonWithIcon("Refresh", theme.ViewRefreshIcon(), refreshNodes)

	healthBtn := widget.NewButtonWithIcon("Health", theme.SearchIcon(), func() {
		go func() {
			version, err := checkAPIHealth(currentSettings())
			if err != nil {
				addLog("API health failed: " + err.Error())
				return
			}
			addLog("API health OK: " + version)
		}()
	})

	testBtn := widget.NewButtonWithIcon("Delay", theme.SearchIcon(), func() {

		name := nodeSelect.Selected
		if name == "" {
			dialog.ShowError(errors.New("select a node first"), w)
			return
		}
		go func() {
			delay, err := testNodeDelay(currentSettings(), name)
			if err != nil {
				addLog("delay test failed: " + err.Error())
				return
			}
			addLog(fmt.Sprintf("%s delay: %d ms", name, delay))
		}()
	})

	proxyOnBtn := widget.NewButtonWithIcon("Proxy On", theme.ConfirmIcon(), func() {
		go func() {
			settings := currentSettings()
			if err := setSystemProxy(settings.ProxyServer, true); err != nil {
				addLog("enable proxy failed: " + err.Error())
				return
			}
			addLog("system proxy enabled at " + settings.ProxyServer)
		}()
	})

	proxyOffBtn := widget.NewButtonWithIcon("Proxy Off", theme.CancelIcon(), func() {
		go func() {
			if err := setSystemProxy("", false); err != nil {
				addLog("disable proxy failed: " + err.Error())
				return
			}
			addLog("system proxy disabled")
		}()
	})

	subscriptionRow := container.NewBorder(nil, nil, nil, addSubBtn, subInput)
	apiRow := container.NewBorder(nil, nil, widget.NewLabel("API"), container.NewHBox(healthBtn, refreshBtn), apiInput)
	secretRow := container.NewBorder(nil, nil, widget.NewLabel("Secret"), nil, secretInput)
	proxyRow := container.NewBorder(nil, nil, widget.NewLabel("Proxy"), nil, proxyInput)
	controlRow := container.NewHBox(statusLabel, layout.NewSpacer(), startBtn, stopBtn, proxyOnBtn, proxyOffBtn)
	nodeRow := container.NewBorder(nil, nil, widget.NewLabel("Node"), container.NewHBox(testBtn), nodeSelect)

	content := container.NewVBox(subscriptionRow, apiRow, secretRow, proxyRow, controlRow, nodeRow, logText)
	w.SetContent(content)

	if desk, ok := a.(desktop.App); ok {
		menu := fyne.NewMenu(appTitle,
			fyne.NewMenuItem("Show", func() { w.Show() }),
			fyne.NewMenuItem("Start Core", func() { go startClash() }),
			fyne.NewMenuItem("Stop Core", func() { stopClash() }),
			fyne.NewMenuItem("Quit", func() {
				stopClash()
				a.Quit()
			}),
		)
		desk.SetSystemTrayMenu(menu)
	}

	w.SetCloseIntercept(func() { w.Hide() })
	w.ShowAndRun()
	stopClash()
}

func addLog(message string) {
	logState.Lock()
	defer logState.Unlock()

	line := fmt.Sprintf("[%s] %s", time.Now().Format("15:04:05"), message)
	logState.lines = append(logState.lines, line)
	if len(logState.lines) > 500 {
		logState.lines = logState.lines[len(logState.lines)-500:]
	}
	_ = logState.text.Set(strings.Join(logState.lines, "\n"))
}

func startClash() error {
	state.Lock()
	if state.running {
		state.Unlock()
		addLog("core already running")
		return nil
	}
	state.Unlock()

	exeDir, err := os.Executable()
	if err != nil {
		return err
	}
	workDir := filepath.Dir(exeDir)
	corePath := filepath.Join(workDir, coreFileName)
	configPath := filepath.Join(workDir, configFileName)

	if _, err := os.Stat(corePath); err != nil {
		return fmt.Errorf("%s not found beside app executable", coreFileName)
	}
	if _, err := os.Stat(configPath); err != nil {
		return fmt.Errorf("%s not found beside app executable", configFileName)
	}

	cmd := exec.Command(corePath, "-f", configPath)
	cmd.Dir = workDir

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}
	if err := cmd.Start(); err != nil {
		return err
	}

	state.Lock()
	state.cmd = cmd
	state.running = true
	state.Unlock()
	addLog("Clash core started")

	go pipeReader(stdout)
	go pipeReader(stderr)
	go func() {
		_ = cmd.Wait()
		state.Lock()
		if state.cmd == cmd {
			state.cmd = nil
			state.running = false
		}
		state.Unlock()
		addLog("Clash core exited")
	}()

	return nil
}

func stopClash() bool {
	state.Lock()
	defer state.Unlock()

	if !state.running || state.cmd == nil || state.cmd.Process == nil {
		return false
	}
	_ = state.cmd.Process.Kill()
	state.cmd = nil
	state.running = false
	addLog("stop signal sent to core")
	return true
}

func pipeReader(r io.ReadCloser) {
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		addLog(scanner.Text())
	}
	if err := scanner.Err(); err != nil {
		addLog("log pipe error: " + err.Error())
	}
}

func fetchSubscription(subscriptionURL string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, subscriptionURL, nil)
	if err != nil {
		return err
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("subscription returned HTTP %d", resp.StatusCode)
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	if len(bytes.TrimSpace(data)) == 0 {
		return errors.New("subscription response is empty")
	}

	exePath, err := os.Executable()
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(filepath.Dir(exePath), configFileName), data, 0644)
}

func checkAPIHealth(settings apiSettings) (string, error) {
	var response map[string]any
	if err := getJSON(settings, "/version", &response); err != nil {
		return "", err
	}
	if version, ok := response["version"].(string); ok && version != "" {
		return version, nil
	}
	return "reachable", nil
}

func loadNodeNames(settings apiSettings) ([]string, string, error) {
	var response proxiesResponse
	if err := getJSON(settings, "/proxies", &response); err != nil {
		return nil, "", err
	}

	global, ok := response.Proxies["GLOBAL"]
	if ok && len(global.All) > 0 {
		names := append([]string(nil), global.All...)
		sort.Strings(names)
		return names, global.Now, nil
	}

	names := make([]string, 0, len(response.Proxies))
	for name, proxy := range response.Proxies {
		switch strings.ToLower(proxy.Type) {
		case "direct", "reject", "selector", "urltest", "fallback", "loadbalance":
			continue
		default:
			names = append(names, name)
		}
	}
	sort.Strings(names)
	return names, "", nil
}

func selectGlobalNode(settings apiSettings, nodeName string) error {
	payload, _ := json.Marshal(map[string]string{"name": nodeName})
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	req, err := newAPIRequest(ctx, http.MethodPut, settings, "/proxies/GLOBAL", bytes.NewReader(payload))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("Clash API returned HTTP %d", resp.StatusCode)
	}
	return nil
}

func testNodeDelay(settings apiSettings, nodeName string) (int, error) {
	endpoint := fmt.Sprintf("/proxies/%s/delay?timeout=5000&url=%s", url.PathEscape(nodeName), url.QueryEscape(testURL))
	var response delayResponse
	if err := getJSON(settings, endpoint, &response); err != nil {
		return 0, err
	}
	return response.Delay, nil
}

func getJSON(settings apiSettings, endpoint string, target any) error {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	req, err := newAPIRequest(ctx, http.MethodGet, settings, endpoint, nil)
	if err != nil {
		return err
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("HTTP %d from %s", resp.StatusCode, endpoint)
	}
	return json.NewDecoder(resp.Body).Decode(target)
}

func newAPIRequest(ctx context.Context, method string, settings apiSettings, endpoint string, body io.Reader) (*http.Request, error) {
	baseURL := strings.TrimRight(strings.TrimSpace(settings.BaseURL), "/")
	if baseURL == "" {
		baseURL = apiBaseURL
	}
	if !strings.HasPrefix(endpoint, "/") {
		endpoint = "/" + endpoint
	}
	req, err := http.NewRequestWithContext(ctx, method, baseURL+endpoint, body)
	if err != nil {
		return nil, err
	}
	if settings.Secret != "" {
		req.Header.Set("Authorization", "Bearer "+settings.Secret)
	}
	return req, nil
}

func setSystemProxy(proxyServer string, enable bool) error {
	regPath := `HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings`
	if enable {
		if strings.TrimSpace(proxyServer) == "" {
			return errors.New("proxy server is empty")
		}
		if err := exec.Command("reg", "add", regPath, "/v", "ProxyEnable", "/t", "REG_DWORD", "/d", "1", "/f").Run(); err != nil {
			return err
		}
		if err := exec.Command("reg", "add", regPath, "/v", "ProxyServer", "/t", "REG_SZ", "/d", proxyServer, "/f").Run(); err != nil {
			return err
		}
		return nil
	}
	return exec.Command("reg", "add", regPath, "/v", "ProxyEnable", "/t", "REG_DWORD", "/d", "0", "/f").Run()
}
