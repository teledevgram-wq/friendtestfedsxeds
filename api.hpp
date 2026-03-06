<?php
// Настройка CORS для работы с фронтендом
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Methods: GET, POST, OPTIONS");
header("Access-Control-Allow-Headers: Content-Type");
header("Content-Type: application/json");

// Обработка preflight запросов
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit();
}

// Подключение к SQLite
try {
    $db = new PDO('sqlite:' . __DIR__ . '/database.sqlite');
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    
    // Создание таблиц, если их нет
    $db->exec("
        CREATE TABLE IF NOT EXISTS tests (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            author TEXT NOT NULL,
            creator_id TEXT,
            creator_name TEXT,
            questions TEXT NOT NULL,
            quotes TEXT NOT NULL,
            created INTEGER NOT NULL
        )
    ");
    
    $db->exec("
        CREATE TABLE IF NOT EXISTS results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            test_id TEXT NOT NULL,
            user_id TEXT,
            user_name TEXT,
            score INTEGER NOT NULL,
            percentage INTEGER NOT NULL,
            created INTEGER NOT NULL,
            FOREIGN KEY (test_id) REFERENCES tests(id)
        )
    ");
    
} catch (PDOException $e) {
    sendResponse('error', 'Database connection failed: ' . $e->getMessage());
    exit();
}

// Получение метода запроса
$method = $_SERVER['REQUEST_METHOD'];
$action = $_GET['action'] ?? '';

// Маршрутизация
switch ($method) {
    case 'GET':
        if ($action === 'get_test') {
            getTest($db);
        } elseif ($action === 'get_stats') {
            getStats($db);
        } else {
            sendResponse('error', 'Invalid action');
        }
        break;
        
    case 'POST':
        $data = json_decode(file_get_contents('php://input'), true);
        
        if ($action === 'save_test') {
            saveTest($db, $data);
        } elseif ($action === 'save_result') {
            saveResult($db, $data);
        } else {
            sendResponse('error', 'Invalid action');
        }
        break;
        
    default:
        sendResponse('error', 'Method not allowed');
}

// Функция для отправки ответа
function sendResponse($status, $data, $message = '') {
    echo json_encode([
        'status' => $status,
        'data' => $data,
        'message' => $message
    ]);
}

// Сохранение теста
function saveTest($db, $data) {
    try {
        // Валидация
        if (empty($data['title']) || empty($data['author']) || empty($data['questions'])) {
            sendResponse('error', null, 'Missing required fields');
            return;
        }
        
        // Генерация ID
        $testId = 'test_' . time() . '_' . bin2hex(random_bytes(4));
        
        // Подготовка данных
        $stmt = $db->prepare("
            INSERT INTO tests (id, title, author, creator_id, creator_name, questions, quotes, created)
            VALUES (:id, :title, :author, :creator_id, :creator_name, :questions, :quotes, :created)
        ");
        
        $stmt->execute([
            ':id' => $testId,
            ':title' => $data['title'],
            ':author' => $data['author'],
            ':creator_id' => $data['creator']['id'] ?? null,
            ':creator_name' => $data['creator']['name'] ?? null,
            ':questions' => json_encode($data['questions'], JSON_UNESCAPED_UNICODE),
            ':quotes' => json_encode($data['quotes'], JSON_UNESCAPED_UNICODE),
            ':created' => time()
        ]);
        
        sendResponse('success', ['test_id' => $testId, 'url' => getTestUrl($testId)], 'Test saved successfully');
        
    } catch (PDOException $e) {
        sendResponse('error', null, 'Database error: ' . $e->getMessage());
    }
}

// Получение теста
function getTest($db) {
    $testId = $_GET['test_id'] ?? '';
    
    if (empty($testId)) {
        sendResponse('error', null, 'Test ID required');
        return;
    }
    
    try {
        $stmt = $db->prepare("SELECT * FROM tests WHERE id = :id");
        $stmt->execute([':id' => $testId]);
        $test = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($test) {
            // Декодируем JSON поля
            $test['questions'] = json_decode($test['questions'], true);
            $test['quotes'] = json_decode($test['quotes'], true);
            
            // Добавляем статистику прохождений
            $statsStmt = $db->prepare("
                SELECT COUNT(*) as total, AVG(percentage) as avg_percentage 
                FROM results WHERE test_id = :test_id
            ");
            $statsStmt->execute([':test_id' => $testId]);
            $stats = $statsStmt->fetch(PDO::FETCH_ASSOC);
            
            $test['stats'] = [
                'total_plays' => $stats['total'] ?? 0,
                'avg_percentage' => round($stats['avg_percentage'] ?? 0)
            ];
            
            sendResponse('success', $test);
        } else {
            sendResponse('error', null, 'Test not found');
        }
        
    } catch (PDOException $e) {
        sendResponse('error', null, 'Database error: ' . $e->getMessage());
    }
}

// Сохранение результата прохождения
function saveResult($db, $data) {
    try {
        if (empty($data['test_id']) || !isset($data['score']) || !isset($data['percentage'])) {
            sendResponse('error', null, 'Missing required fields');
            return;
        }
        
        $stmt = $db->prepare("
            INSERT INTO results (test_id, user_id, user_name, score, percentage, created)
            VALUES (:test_id, :user_id, :user_name, :score, :percentage, :created)
        ");
        
        $stmt->execute([
            ':test_id' => $data['test_id'],
            ':user_id' => $data['user_id'] ?? null,
            ':user_name' => $data['user_name'] ?? 'Anonymous',
            ':score' => $data['score'],
            ':percentage' => $data['percentage'],
            ':created' => time()
        ]);
        
        sendResponse('success', ['result_id' => $db->lastInsertId()], 'Result saved');
        
    } catch (PDOException $e) {
        sendResponse('error', null, 'Database error: ' . $e->getMessage());
    }
}

// Получение статистики
function getStats($db) {
    $testId = $_GET['test_id'] ?? '';
    
    try {
        if ($testId) {
            // Статистика по конкретному тесту
            $stmt = $db->prepare("
                SELECT 
                    COUNT(*) as total_plays,
                    AVG(percentage) as avg_percentage,
                    MAX(percentage) as max_percentage,
                    MIN(percentage) as min_percentage,
                    COUNT(DISTINCT user_id) as unique_users
                FROM results 
                WHERE test_id = :test_id
            ");
            $stmt->execute([':test_id' => $testId]);
        } else {
            // Общая статистика
            $stmt = $db->query("
                SELECT 
                    COUNT(DISTINCT test_id) as total_tests,
                    COUNT(*) as total_plays,
                    AVG(percentage) as avg_percentage
                FROM results
            ");
        }
        
        $stats = $stmt->fetch(PDO::FETCH_ASSOC);
        sendResponse('success', $stats);
        
    } catch (PDOException $e) {
        sendResponse('error', null, 'Database error: ' . $e->getMessage());
    }
}

// Вспомогательная функция для получения URL теста
function getTestUrl($testId) {
    $protocol = isset($_SERVER['HTTPS']) ? 'https://' : 'http://';
    $host = $_SERVER['HTTP_HOST'];
    $path = dirname($_SERVER['SCRIPT_NAME']);
    return $protocol . $host . $path . '/?test=' . $testId;
}
?>
