"""Complete-observation-window oracle, independent of RTL accumulator widths."""
class ConfidenceModel:
    def __init__(self):
        self.ready = False
        self.window = []

    def step(self, error, period, valid, fault, reset):
        if reset or fault:
            self.ready = False
            self.window.clear()
        elif valid:
            self.window.append((error, period))
            if len(self.window) == 1024:
                errors = [e for e, _ in self.window]
                partials = [sum(errors[i:i + 128]) for i in range(0, 1024, 128)]
                self.ready = (max(map(abs, partials)) <= 27487 and abs(sum(errors)) <= 27487
                              and abs(self.window[-1][1] - self.window[0][1]) <= 537
                              and sum(e != 0 for e in errors) >= 128
                              and sum(map(abs, errors)) <= 2097152)
                self.window.clear()
