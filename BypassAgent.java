import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.Field;
import java.security.ProtectionDomain;
import java.util.Collections;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class BypassAgent {
    
    private static volatile boolean active = false;
    private static ScheduledExecutorService scheduler;
    
    public static void premain(String args, Instrumentation inst) {
        System.out.println("[BypassAgent] Loading...");
        activate(inst);
    }
    
    public static void agentmain(String args, Instrumentation inst) {
        System.out.println("[BypassAgent] Attached dynamically...");
        activate(inst);
    }
    
    private static void activate(Instrumentation inst) {
        if (active) return;
        active = true;
        
        try {
            // Ждём загрузки классов античита
            Thread waiter = new Thread(() -> {
                try {
                    // Ждём до 30 секунд загрузки ClientAntiCheat
                    for (int i = 0; i < 60; i++) {
                        try {
                            Class.forName("net.rustmine.client.anticheat.ClientAntiCheat");
                            break;
                        } catch (ClassNotFoundException e) {
                            Thread.sleep(500);
                        }
                    }
                    
                    Class<?> acClass = Class.forName("net.rustmine.client.anticheat.ClientAntiCheat");
                    System.out.println("[BypassAgent] ClientAntiCheat found, applying bypasses...");
                    
                    // 1. Очистка trippedTraps каждые 5мс (быстрее чем tick rate ~16мс)
                    Field trippedTrapsField = acClass.getDeclaredField("trippedTraps");
                    trippedTrapsField.setAccessible(true);
                    Object trippedTraps = trippedTrapsField.get(null);
                    
                    scheduler = Executors.newSingleThreadScheduledExecutor(r -> {
                        Thread t = new Thread(r, "bypass-cleaner");
                        t.setDaemon(true);
                        return t;
                    });
                    
                    scheduler.scheduleAtFixedRate(() -> {
                        try {
                            if (trippedTraps instanceof Set) {
                                ((Set<?>) trippedTraps).clear();
                            }
                        } catch (Exception e) {}
                    }, 0, 5, TimeUnit.MILLISECONDS);
                    
                    System.out.println("[BypassAgent] Memory traps bypass active (clearing every 5ms)");
                    
                    // 2. Сброс счётчиков мыши каждые 5мс
                    Field injectedMovesField = acClass.getDeclaredField("injectedMouseMoves");
                    injectedMovesField.setAccessible(true);
                    Field realIdleMovesField = acClass.getDeclaredField("realIdleMoves");
                    realIdleMovesField.setAccessible(true);
                    Field injectedIdleMovesField = acClass.getDeclaredField("injectedIdleMoves");
                    injectedIdleMovesField.setAccessible(true);
                    
                    scheduler.scheduleAtFixedRate(() -> {
                        try {
                            injectedMovesField.set(null, 0);
                            injectedIdleMovesField.set(null, 0);
                            // Накручиваем realIdleMoves чтобы условие не сработало
                            int current = (Integer) realIdleMovesField.get(null);
                            if (current < 200) {
                                realIdleMovesField.set(null, current + 10);
                            }
                        } catch (Exception e) {}
                    }, 0, 5, TimeUnit.MILLISECONDS);
                    
                    System.out.println("[BypassAgent] Mouse hook bypass active (resetting every 5ms)");
                    
                    // 3. Подмена signatures на пустой JsonObject
                    try {
                        Field signaturesField = acClass.getDeclaredField("signatures");
                        signaturesField.setAccessible(true);
                        com.google.gson.JsonObject emptyJson = new com.google.gson.JsonObject();
                        signaturesField.set(null, emptyJson);
                        System.out.println("[BypassAgent] Signatures replaced with empty JsonObject");
                    } catch (Exception e) {
                        System.err.println("[BypassAgent] Signatures bypass failed: " + e.getMessage());
                    }
                    
                    // 4. НЕ меняем поля recoilScale, coneScale, ignoreRecoil и т.д.
                    // Это триггерит checkMemoryTraps() каждый тик
                    // Вместо этого используем C++ компенсацию recoil через vortex.dll
                    
                    System.out.println("[BypassAgent] All bypasses active!");
                    System.out.println("[BypassAgent] Using C++ recoil compensation (no field modification)");
                    
                } catch (Exception e) {
                    System.err.println("[BypassAgent] Error: " + e.getMessage());
                    e.printStackTrace();
                }
            });
            waiter.setDaemon(true);
            waiter.start();
            
        } catch (Exception e) {
            System.err.println("[BypassAgent] Fatal error: " + e.getMessage());
        }
    }
}