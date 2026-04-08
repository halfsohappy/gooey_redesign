# State Machine
# Switch between idle/active/cooldown based on motion

if "mode" not in state:
    state["mode"] = "idle"
    state["timer"] = 0

accel = sensor("accelLength")
d = dt()

if state["mode"] == "idle":
    if accel > 0.7:
        state["mode"] = "active"
        print("-> ACTIVE")

elif state["mode"] == "active":
    osc_send("192.168.1.50", 7000, "/active", accel)
    if accel < 0.3:
        state["mode"] = "cooldown"
        state["timer"] = 2.0  # 2 second cooldown
        print("-> COOLDOWN")

elif state["mode"] == "cooldown":
    state["timer"] -= d
    if state["timer"] <= 0:
        state["mode"] = "idle"
        print("-> IDLE")
