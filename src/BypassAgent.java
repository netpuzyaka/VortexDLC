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

        // Регистрируем трансформер для патчинга байт-кода
        inst.addTransformer(new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader, String className, Class<?> classBeingRedefined,
                                    ProtectionDomain protectionDomain, byte[] classfileBuffer) {
                if (className == null) return null;
                String cn = className.replace('/', '.');
                
                // Патчим HitscanGun.recoil() -> пустой метод
                if (cn.equals("net.rustmine.client.HitscanGun")) {
                    return patchMethod(classfileBuffer, "recoil", "(Lnet/minecraft/class_746;I)V");
                }
                // Патчим ViewRecoil.addPunch() -> пустой метод
                if (cn.equals("net.rustmine.client.ViewRecoil")) {
                    return patchMethod(classfileBuffer, "addPunch", "(FFF)V");
                }
                // Патчим ClientAntiCheat.send() -> отправляет чёрный массив
                if (cn.equals("net.rustmine.client.anticheat.ClientAntiCheat")) {
                    return patchSendMethod(classfileBuffer);
                }
                return null;
            }
        }, true); // canRetransform = true

        // Запускаем фоновые задачи очистки
        Thread waiter = new Thread(() -> {
            try {
                // Ждём загрузки ClientAntiCheat
                for (int i = 0; i < 60; i++) {
                    try {
                        Class.forName("net.rustmine.client.anticheat.ClientAntiCheat");
                        break;
                    } catch (ClassNotFoundException e) {
                        Thread.sleep(500);
                    }
                }

                Class<?> acClass = Class.forName("net.rustmine.client.anticheat.ClientAntiCheat");
                System.out.println("[BypassAgent] ClientAntiCheat found");

                // 1. Очистка trippedTraps каждые 5мс
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
                System.out.println("[BypassAgent] trippedTraps cleared every 5ms");

                // 2. Сброс счётчиков мыши
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
                        int current = (Integer) realIdleMovesField.get(null);
                        if (current < 200) {
                            realIdleMovesField.set(null, current + 10);
                        }
                    } catch (Exception e) {}
                }, 0, 5, TimeUnit.MILLISECONDS);
                System.out.println("[BypassAgent] mouse counters neutralized");

                // 3. Установка signatures в null (scan() сам создаст пустой JsonObject при необходимости)
                try {
                     Field signaturesField = acClass.getDeclaredField("signatures");
                     signaturesField.setAccessible(true);
                     signaturesField.set(null, null);
                     System.out.println("[BypassAgent] signatures = null (safe)");
                } catch (Exception e) {
                     System.err.println("[BypassAgent] signatures bypass failed: " + e.getMessage());
                }

                // 4. ВАЖНО: НЕ меняем поля HitscanGun.recoilScale, ViewRecoil.disableRecoil и т.д.
                // Это ловушки checkMemoryTraps()!

                System.out.println("[BypassAgent] All bypasses active!");

            } catch (Exception e) {
                System.err.println("[BypassAgent] Error: " + e.getMessage());
                e.printStackTrace();
            }
        });
        waiter.setDaemon(true);
        waiter.start();
    }

    // Патчинг метода: заменяем код на 'return' (байт 0xB1 для void)
    private static byte[] patchMethod(byte[] classBytes, String methodName, String descriptor) {
        try {
            // Упрощённый парсер: ищем имя метода в constant pool
            // Формат class file: magic(4) + version(4) + constant_pool_count(2) + constant_pool...
            
            int cpCount = ((classBytes[8] & 0xFF) << 8) | (classBytes[9] & 0xFF);
            
            // Ищем Utf8 entries с именем метода
            int methodRefIdx = -1;
            int nameIdx = -1;
            int descIdx = -1;
            
            // Находим индексы в constant pool
            int offset = 10;
            for (int i = 1; i < cpCount; i++) {
                int tag = classBytes[offset] & 0xFF;
                if (tag == 1) { // Utf8
                    int len = ((classBytes[offset+1] & 0xFF) << 8) | (classBytes[offset+2] & 0xFF);
                    String s = new String(classBytes, offset+3, len, "UTF-8");
                    if (s.equals(methodName)) nameIdx = i;
                    if (s.equals(descriptor)) descIdx = i;
                    offset += 3 + len;
                } else if (tag == 7 || tag == 8 || tag == 16 || tag == 19 || tag == 20) {
                    offset += 3;
                } else if (tag == 9 || tag == 10 || tag == 11 || tag == 12 || tag == 18) {
                    offset += 5;
                } else if (tag == 3 || tag == 4 || tag == 14) {
                    offset += 5;
                } else if (tag == 5 || tag == 6) {
                    offset += 9;
                    i++; // занимает 2 слота
                } else if (tag == 15) {
                    offset += 4;
                } else {
                    break; // неизвестный тег
                }
            }
            
            if (nameIdx == -1 || descIdx == -1) {
                System.out.println("[BypassAgent] Method " + methodName + " not found in constant pool");
                return classBytes;
            }
            
            // Ищем method_info структуры после constant pool
            // class file: magic(4) + minor(2) + major(2) + cp_count(2) + cp... + access_flags(2) + this_class(2) + super_class(2) + interfaces_count(2) + interfaces... + fields_count(2) + fields... + methods_count(2) + methods...
            
            // Пропускаем constant pool
            offset = 10;
            for (int i = 1; i < cpCount; i++) {
                int tag = classBytes[offset] & 0xFF;
                if (tag == 1) {
                    int len = ((classBytes[offset+1] & 0xFF) << 8) | (classBytes[offset+2] & 0xFF);
                    offset += 3 + len;
                } else if (tag == 7 || tag == 8 || tag == 16 || tag == 19 || tag == 20) {
                    offset += 3;
                } else if (tag == 9 || tag == 10 || tag == 11 || tag == 12 || tag == 18) {
                    offset += 5;
                } else if (tag == 3 || tag == 4 || tag == 14) {
                    offset += 5;
                } else if (tag == 5 || tag == 6) {
                    offset += 9;
                    i++;
                } else if (tag == 15) {
                    offset += 4;
                } else {
                    break;
                }
            }
            
            // access_flags + this_class + super_class
            offset += 6;
            // interfaces_count + interfaces
            int ifCount = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
            offset += 2 + ifCount * 2;
            // fields_count + fields (упрощённо пропускаем)
            int fieldsCount = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
            offset += 2;
            for (int i = 0; i < fieldsCount; i++) {
                offset += 2; // access_flags
                offset += 2; // name_index
                offset += 2; // descriptor_index
                int attrsCount = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
                offset += 2;
                for (int j = 0; j < attrsCount; j++) {
                    offset += 2; // attribute_name_index
                    int attrLen = ((classBytes[offset] & 0xFF) << 24) | ((classBytes[offset+1] & 0xFF) << 16) | 
                                  ((classBytes[offset+2] & 0xFF) << 8) | (classBytes[offset+3] & 0xFF);
                    offset += 4 + attrLen;
                }
            }
            // methods_count
            int methodsCount = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
            offset += 2;
            
            for (int i = 0; i < methodsCount; i++) {
                int accessFlags = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
                int mNameIdx = ((classBytes[offset+2] & 0xFF) << 8) | (classBytes[offset+3] & 0xFF);
                int mDescIdx = ((classBytes[offset+4] & 0xFF) << 8) | (classBytes[offset+5] & 0xFF);
                
                if (mNameIdx == nameIdx && mDescIdx == descIdx) {
                    // Нашли метод! Теперь ищем Code attribute
                    offset += 6; // пропускаем access_flags + name_index + descriptor_index
                    int attrsCount = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
                    offset += 2;
                    
                    for (int j = 0; j < attrsCount; j++) {
                        int attrNameIdx = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
                        int attrLen = ((classBytes[offset+2] & 0xFF) << 24) | ((classBytes[offset+3] & 0xFF) << 16) | 
                                      ((classBytes[offset+4] & 0xFF) << 8) | (classBytes[offset+5] & 0xFF);
                        
                        // Проверяем имя атрибута - должно быть "Code"
                        // Для простоты: первый атрибут с длиной > 0 обычно Code
                        if (attrLen > 0 && j == 0) {
                            // Code attribute: attribute_name_index(2) + attribute_length(4) + max_stack(2) + max_locals(2) + code_length(4) + code[...] + exception_table_length(2) + ...
                            int codeOffset = offset + 6 + 2 + 2 + 4; // пропускаем заголовок Code
                            int codeLen = ((classBytes[offset+6+2+2] & 0xFF) << 24) | 
                                          ((classBytes[offset+6+2+3] & 0xFF) << 16) | 
                                          ((classBytes[offset+6+2+4] & 0xFF) << 8) | 
                                          (classBytes[offset+6+2+5] & 0xFF);
                            
                            // Заменяем код метода на: return (0xB1)
                            classBytes[codeOffset] = (byte) 0xB1;
                            // Обнуляем остальной код
                            for (int k = 1; k < codeLen; k++) {
                                classBytes[codeOffset + k] = 0;
                            }
                            // Обновляем code_length = 1
                            classBytes[offset+6+2+2] = 0;
                            classBytes[offset+6+2+3] = 0;
                            classBytes[offset+6+2+4] = 0;
                            classBytes[offset+6+2+5] = 1;
                            
                            System.out.println("[BypassAgent] Patched method " + methodName + " -> return");
                            return classBytes;
                        }
                        
                        offset += 6 + attrLen;
                    }
                    return classBytes;
                } else {
                    // Пропускаем метод
                    offset += 6;
                    int attrsCount = ((classBytes[offset] & 0xFF) << 8) | (classBytes[offset+1] & 0xFF);
                    offset += 2;
                    for (int j = 0; j < attrsCount; j++) {
                        offset += 2;
                        int attrLen = ((classBytes[offset] & 0xFF) << 24) | ((classBytes[offset+1] & 0xFF) << 16) | 
                                      ((classBytes[offset+2] & 0xFF) << 8) | (classBytes[offset+3] & 0xFF);
                        offset += 4 + attrLen;
                    }
                }
            }
            
            System.out.println("[BypassAgent] Method " + methodName + " not found");
            return classBytes;
            
        } catch (Exception e) {
            System.err.println("[BypassAgent] patchMethod error: " + e.getMessage());
            return classBytes;
        }
    }

    // Патчинг send() для отправки чёрного скриншота
    private static byte[] patchSendMethod(byte[] classBytes) {
        // send(int id, int w, int h, int[] rgb) -> заменяем rgb на все нули
        // Упрощённо: патчим метод screenshot() чтобы он не вызывал send
        return patchMethod(classBytes, "screenshot", "(I)V");
    }
}