namespace GameFlow
{
    /// <summary>Discards every line. Use to fully silence the SDK.</summary>
    public sealed class NullLogger : IGameFlowLogger
    {
        public void Info(string message) { }
        public void Warn(string message) { }
        public void Error(string message) { }
        public void Debug(string message) { }
    }
}
